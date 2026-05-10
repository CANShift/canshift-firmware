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
    OtaHmacVerifier(const HmacBackend &backend, const uint8_t *secret,
                    size_t secretLen, OtaSinkFn sink, void *sinkUser);

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

} // namespace OtaHmac
