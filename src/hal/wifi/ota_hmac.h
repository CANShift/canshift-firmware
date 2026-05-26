#pragma once
// ota_hmac.h — HMAC-SHA256 trailer verification for OTA uploads
//
// Trailer format: <firmware bytes> || HMAC_SHA256(firmware bytes, secret)
// The HMAC occupies the last 32 bytes of the upload. Everything before it
// is real firmware and gets streamed through the sink (typically Update.write).
//
// The verifier maintains a rolling 32-byte window: bytes that "exit" the
// window go to the sink and into the HMAC computation, the 32 bytes that
// remain in the window at end-of-stream are interpreted as the trailer and
// compared (constant time) against the freshly computed HMAC.
//
// Why a backend abstraction?
// - Production: mbedTLS HMAC-SHA256 (linked when APP_WIFI_OTA_ENABLED=1).
// - Native unit tests: stub backend that proves the framing/streaming logic
//   without dragging crypto into the host build (global rule: never
//   reimplement crypto).
//
// Naming: C-style function pointers throughout — std::function would cost
// ~200 B and could heap-allocate; a function-pointer table is zero-overhead.

#include <stddef.h>
#include <stdint.h>

namespace OtaHmac {

// SHA-256 output size — also the trailer length we look for at end-of-stream.
constexpr size_t kHmacLen = 32;

// Sink for body bytes (everything that is NOT the trailing HMAC). Returning
// false aborts the upload.
using OtaSinkFn = bool (*)(const uint8_t *data, size_t len, void *user);

// HMAC backend interface — opaque ctx is owned by the backend.
// init()      → allocates / initialises a context bound to (secret, secretLen)
// update()    → feeds bytes into the running HMAC
// finalize()  → writes 32 bytes of HMAC output into out, frees the context
struct HmacBackend {
    void *(*init)(const uint8_t *secret, size_t secretLen);
    bool (*update)(void *ctx, const uint8_t *data, size_t len);
    bool (*finalize)(void *ctx, uint8_t out[kHmacLen]);
};

// Constant-time memory compare. Returns 0 iff the two regions are equal.
// Exposed for testing; safe to call from any context.
int constantTimeMemcmp(const uint8_t *a, const uint8_t *b, size_t len);

class OtaHmacVerifier {
  public:
    // backend, secret and sink must outlive the verifier.
    // sinkUser is passed back to the sink unchanged.
    OtaHmacVerifier(const HmacBackend &backend, const uint8_t *secret, size_t secretLen,
                    OtaSinkFn sink, void *sinkUser);

    ~OtaHmacVerifier();

    OtaHmacVerifier(const OtaHmacVerifier &) = delete;
    OtaHmacVerifier &operator=(const OtaHmacVerifier &) = delete;

    // Must be called once before any feed(). Returns false if the backend
    // refused to initialise.
    bool begin();

    // Stream a chunk. Body bytes (everything but the last 32 seen so far)
    // are forwarded to the sink and folded into the HMAC. Returns false on
    // sink or backend failure — verifier becomes unusable in that case.
    bool feed(const uint8_t *data, size_t len);

    // Finalise: the 32 bytes still in the rolling window are taken as the
    // received trailer, compared (constant time) against the computed HMAC.
    // Returns true iff the upload contained at least 32 bytes AND the
    // trailer matched.
    bool finish();

    // Total bytes accepted via feed() (body + trailer).
    size_t totalBytes() const {
        return m_totalBytes;
    }

  private:
    const HmacBackend &m_backend;
    const uint8_t *m_secret;
    size_t m_secretLen;
    OtaSinkFn m_sink;
    void *m_sinkUser;

    void *m_ctx = nullptr;
    bool m_failed = false;
    size_t m_totalBytes = 0;

    // Rolling window of the last bytes seen. Sized for the max possible
    // trailer (kHmacLen). m_windowFill is the count currently buffered.
    uint8_t m_window[kHmacLen]{};
    size_t m_windowFill = 0;
};

#ifndef UNIT_TEST
// Production HMAC-SHA256 backend backed by mbedTLS. Available only when
// building for the device — host tests use a stub backend.
const HmacBackend &mbedtlsHmacBackend();
#endif

// ---------------------------------------------------------------------------
// Per-device key provisioning (issue #521)
// ---------------------------------------------------------------------------
//
// First-boot flow: read NVS namespace `ota` key `hmac_key`. If absent or the
// wrong length, draw 32 fresh random bytes, persist them, and return them.
// If NVS is unwritable (rare — corrupted partition), fall back to the
// build-time `OTA_HMAC_SECRET` macro so legacy installs that already trust
// the embedded key keep working through the rollout.
//
// The 32-byte length matches `kHmacLen` because SHA-256's block size is
// 64 bytes — anything longer would get hashed-then-rekeyed by HMAC anyway,
// so 32 bytes is the largest size that contributes full entropy.

// Provenance of the loaded key — used by the boot diag to tell operators
// whether a device is on a per-device NVS key (good) or still leaning on
// the embedded build-time secret (legacy, fleet-wide).
enum class KeySource : uint8_t {
    Nvs,          // read from NVS — per-device, generated on first boot
    NvsGenerated, // first-boot path — drew new bytes AND persisted to NVS
    Embedded,     // NVS unavailable — fell back to build-time OTA_HMAC_SECRET
};

// Abstraction over the persistent key store. Backed by `Preferences` on
// device; a host fake is used by native unit tests. Read returns the number
// of bytes written into `out` (or 0 if the entry is missing / wrong size).
struct KeyStore {
    size_t (*read)(uint8_t *out, size_t maxLen, void *user);
    bool (*write)(const uint8_t *data, size_t len, void *user);
    void *user;
};

// Random-bytes source. esp_fill_random() on device; injectable for tests.
using RandomFn = void (*)(uint8_t *out, size_t len);

// Core load-or-generate routine. Side-effect-free relative to its inputs
// — no globals — so host tests can exercise every branch.
//
// Returns true on success and writes the 32-byte key into `out`. On
// success, *outSource records where the key came from.
//
// `fallback` may be null (no embedded key available); in that case a
// missing/unwritable NVS produces a return value of false.
bool loadOrGenerateKey(const KeyStore &store, RandomFn rng, const uint8_t *fallback,
                       size_t fallbackLen, uint8_t out[kHmacLen], KeySource *outSource);

// Render the first 8 hex chars of SHA-256(key) into `out` (9 bytes incl.
// trailing NUL). Used by the boot diag so the actual key never appears in
// logs. `out` must point to ≥ 9 bytes. Returns false if sha-256 init fails.
//
// On the host (UNIT_TEST), no mbedTLS is linked — this helper is only
// declared on the device build to keep host tests self-contained.
#ifndef UNIT_TEST
bool computeKeyFingerprint(const uint8_t *key, size_t keyLen, char out[9]);
#endif

#ifndef UNIT_TEST
// Production-side wrapper: opens the `ota` Preferences namespace, defers
// to loadOrGenerateKey(), and caches the result for the lifetime of the
// process. Called once at OTA-verifier construction time; subsequent calls
// are cheap and return the cached key. Returns a pointer to a static
// 32-byte buffer — caller must NOT free.
//
// `outSource` is optional; when non-null it reports the provenance of the
// returned key (cached on first call, returned identically thereafter).
const uint8_t *loadOrGenerateKey(KeySource *outSource = nullptr);

// One-shot boot diagnostic — resolves the in-use key (triggering NVS
// load-or-generate the first time) and logs its SHA-256 fingerprint plus
// provenance. Lets operators correlate which devices have rolled over to
// per-device NVS keys vs which still ride the embedded fallback, without
// exposing the actual key bytes. Safe to call multiple times — only logs
// once. Issue #521.
void logBootKeyFingerprint();
#endif

} // namespace OtaHmac
