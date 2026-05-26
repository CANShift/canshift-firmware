// ota_hmac.cpp — rolling-window HMAC verifier for OTA uploads.
//
// See ota_hmac.h for the trailer format and design rationale. The streaming
// logic here is platform-agnostic and exercised by host unit tests through a
// stub backend; the mbedTLS backend at the bottom of this file is the only
// part that needs the ESP-IDF.

#include "hal/wifi/ota_hmac.h"

#include <string.h>

#if !defined(UNIT_TEST)
    #include "app_config.h" // OTA_HMAC_SECRET embedded fallback (issue #521)
    #include "diag/logger.h"
    #include <Preferences.h>
    #include <esp_random.h>
    #include <mbedtls/sha256.h>
#endif

namespace OtaHmac {

int constantTimeMemcmp(const uint8_t *a, const uint8_t *b, size_t len) {
    uint8_t diff = 0;
    for (size_t i = 0; i < len; ++i) {
        diff |= static_cast<uint8_t>(a[i] ^ b[i]);
    }
    // Fold the OR-accumulator into a 0/1 result without branching on its
    // value. Any non-zero diff produces a non-zero return.
    return diff;
}

OtaHmacVerifier::OtaHmacVerifier(const HmacBackend &backend, const uint8_t *secret,
                                 size_t secretLen, OtaSinkFn sink, void *sinkUser)
    : m_backend(backend), m_secret(secret), m_secretLen(secretLen), m_sink(sink),
      m_sinkUser(sinkUser) {}

OtaHmacVerifier::~OtaHmacVerifier() {
    if (m_ctx != nullptr) {
        // Drop any pending HMAC state — we don't care about the output here,
        // we just need the backend to release its allocations.
        uint8_t scratch[kHmacLen];
        m_backend.finalize(m_ctx, scratch);
        m_ctx = nullptr;
    }
}

bool OtaHmacVerifier::begin() {
    if (m_ctx != nullptr || m_failed) {
        m_failed = true;
        return false;
    }
    m_ctx = m_backend.init(m_secret, m_secretLen);
    if (m_ctx == nullptr) {
        m_failed = true;
        return false;
    }
    return true;
}

bool OtaHmacVerifier::feed(const uint8_t *data, size_t len) {
    if (m_failed || m_ctx == nullptr) {
        m_failed = true;
        return false;
    }
    if (len == 0) {
        return true;
    }
    m_totalBytes += len;

    // Strategy: concatenate window || data conceptually, then push everything
    // except the last kHmacLen bytes through the sink and into the HMAC.
    // Anything left over becomes the new window.
    const size_t available = m_windowFill + len;
    if (available <= kHmacLen) {
        // Not enough total bytes yet to flush anything — just append to the
        // window.
        memcpy(m_window + m_windowFill, data, len);
        m_windowFill = available;
        return true;
    }

    // Bytes that need to leave the window and be written.
    const size_t toEmit = available - kHmacLen;

    // Phase 1: drain bytes that are currently in m_window. At most kHmacLen
    // (i.e. m_windowFill).
    const size_t fromWindow = (toEmit < m_windowFill) ? toEmit : m_windowFill;
    if (fromWindow > 0) {
        if (!m_sink(m_window, fromWindow, m_sinkUser)) {
            m_failed = true;
            return false;
        }
        if (!m_backend.update(m_ctx, m_window, fromWindow)) {
            m_failed = true;
            return false;
        }
    }

    // Phase 2: any remaining emit comes from the head of the new chunk.
    const size_t fromData = toEmit - fromWindow;
    if (fromData > 0) {
        if (!m_sink(data, fromData, m_sinkUser)) {
            m_failed = true;
            return false;
        }
        if (!m_backend.update(m_ctx, data, fromData)) {
            m_failed = true;
            return false;
        }
    }

    // Rebuild the window with the last kHmacLen bytes of (window || data).
    // The leftover from the original window (if any) shifts to the front,
    // and the tail of the new chunk fills the rest.
    const size_t windowLeftover = m_windowFill - fromWindow;
    if (windowLeftover > 0) {
        memmove(m_window, m_window + fromWindow, windowLeftover);
    }
    const size_t fromDataToWindow = len - fromData;
    if (fromDataToWindow > 0) {
        memcpy(m_window + windowLeftover, data + fromData, fromDataToWindow);
    }
    m_windowFill = windowLeftover + fromDataToWindow;
    return true;
}

bool OtaHmacVerifier::finish() {
    if (m_failed || m_ctx == nullptr) {
        m_failed = true;
        return false;
    }
    if (m_windowFill != kHmacLen) {
        // Upload was shorter than the trailer itself — definitely invalid.
        m_failed = true;
        return false;
    }
    uint8_t computed[kHmacLen];
    const bool finalizeOk = m_backend.finalize(m_ctx, computed);
    m_ctx = nullptr; // backend has released its context
    if (!finalizeOk) {
        m_failed = true;
        return false;
    }
    return constantTimeMemcmp(computed, m_window, kHmacLen) == 0;
}

// ---------------------------------------------------------------------------
// Per-device key provisioning (issue #521)
// ---------------------------------------------------------------------------

bool loadOrGenerateKey(const KeyStore &store, RandomFn rng, const uint8_t *fallback,
                       size_t fallbackLen, uint8_t out[kHmacLen], KeySource *outSource) {
    // 1. Try NVS read. A full-size match is the steady-state happy path.
    const size_t readBytes = store.read(out, kHmacLen, store.user);
    if (readBytes == kHmacLen) {
        if (outSource != nullptr)
            *outSource = KeySource::Nvs;
        return true;
    }

    // 2. Missing or wrong size — generate fresh bytes and persist. If either
    //    step fails we fall through to the embedded fallback.
    if (rng != nullptr) {
        rng(out, kHmacLen);
        if (store.write(out, kHmacLen, store.user)) {
            if (outSource != nullptr)
                *outSource = KeySource::NvsGenerated;
            return true;
        }
    }

    // 3. NVS write failed (or no RNG was supplied) — last resort, copy the
    //    build-time embedded secret into the output. This keeps legacy
    //    installs that never seeded NVS alive through the rollout window.
    //    TODO(#521): remove the embedded-fallback branch once the fleet has
    //    rolled over to NVS-resident keys.
    if (fallback != nullptr && fallbackLen > 0) {
        const size_t copy = (fallbackLen < kHmacLen) ? fallbackLen : kHmacLen;
        memcpy(out, fallback, copy);
        // If the embedded secret is shorter than 32 bytes, zero-pad the rest
        // — HMAC accepts arbitrary key lengths, so this preserves all the
        // entropy the operator put in secrets.ini.
        if (copy < kHmacLen) {
            memset(out + copy, 0, kHmacLen - copy);
        }
        if (outSource != nullptr)
            *outSource = KeySource::Embedded;
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Production backend — mbedTLS. Excluded from:
//   - native unit tests (UNIT_TEST), which use a stub backend
//   - USE_RUST_OTA_HMAC builds (#827 Phase 3), where ota_hmac_bridge.cpp
//     provides a Rust-backed HmacBackend instead. Both definitions of
//     `mbedtlsHmacBackend()` would collide at link time otherwise.
// ---------------------------------------------------------------------------
#if !defined(UNIT_TEST) && !defined(USE_RUST_OTA_HMAC)

    #include <mbedtls/md.h>

namespace {

mbedtls_md_context_t s_mdContext;
bool s_mdContextInUse = false;

void *mbedInit(const uint8_t *secret, size_t secretLen) {
    // OTA is single-instance; mbedtls backend matches.
    if (s_mdContextInUse)
        return nullptr;

    mbedtls_md_init(&s_mdContext);
    const mbedtls_md_info_t *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (info == nullptr)
        return nullptr;
    if (mbedtls_md_setup(&s_mdContext, info, /*hmac=*/1) != 0) {
        mbedtls_md_free(&s_mdContext);
        return nullptr;
    }
    if (mbedtls_md_hmac_starts(&s_mdContext, secret, secretLen) != 0) {
        mbedtls_md_free(&s_mdContext);
        return nullptr;
    }
    s_mdContextInUse = true;
    return &s_mdContext;
}

bool mbedUpdate(void *ctx, const uint8_t *data, size_t len) {
    return mbedtls_md_hmac_update(static_cast<mbedtls_md_context_t *>(ctx), data, len) == 0;
}

bool mbedFinalize(void *ctx, uint8_t out[kHmacLen]) {
    auto *mdCtx = static_cast<mbedtls_md_context_t *>(ctx);
    const int rc = mbedtls_md_hmac_finish(mdCtx, out);
    mbedtls_md_free(mdCtx);
    s_mdContextInUse = false;
    return rc == 0;
}

const HmacBackend kMbedBackend = {mbedInit, mbedUpdate, mbedFinalize};

} // namespace

const HmacBackend &mbedtlsHmacBackend() {
    return kMbedBackend;
}

#endif // !UNIT_TEST && !USE_RUST_OTA_HMAC

// ---------------------------------------------------------------------------
// Per-device key — Preferences-backed KeyStore and process-lifetime cache.
// Lives outside the Rust gate because the key provisioning path is shared
// by both backends (Rust and mbedTLS HMACs both take the same 32 bytes).
// ---------------------------------------------------------------------------
#if !defined(UNIT_TEST)

namespace {

constexpr char kNvsNamespace[] = "ota";
constexpr char kNvsKey[] = "hmac_key";

size_t prefsRead(uint8_t *out, size_t maxLen, void * /*user*/) {
    Preferences p;
    if (!p.begin(kNvsNamespace, /*readOnly=*/true)) {
        return 0;
    }
    const size_t storedLen = p.getBytesLength(kNvsKey);
    if (storedLen != kHmacLen || storedLen > maxLen) {
        p.end();
        return 0;
    }
    const size_t copied = p.getBytes(kNvsKey, out, kHmacLen);
    p.end();
    return copied;
}

bool prefsWrite(const uint8_t *data, size_t len, void * /*user*/) {
    Preferences p;
    if (!p.begin(kNvsNamespace, /*readOnly=*/false)) {
        return false;
    }
    const size_t written = p.putBytes(kNvsKey, data, len);
    p.end();
    return written == len;
}

void hwRandom(uint8_t *out, size_t len) {
    esp_fill_random(out, len);
}

// Process-lifetime cache for the resolved key. Populated lazily on first
// call to loadOrGenerateKey() so we touch NVS at most once per boot.
uint8_t s_cachedKey[kHmacLen] = {};
bool s_cachedKeyValid = false;
KeySource s_cachedSource = KeySource::Embedded;

} // namespace

const uint8_t *loadOrGenerateKey(KeySource *outSource) {
    if (!s_cachedKeyValid) {
        const KeyStore store = {prefsRead, prefsWrite, nullptr};
        // The embedded macro lives in app_config.h; it is always a NUL-
        // terminated C string. Pass it without the trailing NUL so the byte
        // count matches what the legacy code path used in handleOtaUpload.
        static const char kEmbedded[] = OTA_HMAC_SECRET;
        constexpr size_t kEmbeddedLen = sizeof(kEmbedded) - 1;
        const bool ok =
            loadOrGenerateKey(store, hwRandom, reinterpret_cast<const uint8_t *>(kEmbedded),
                              kEmbeddedLen, s_cachedKey, &s_cachedSource);
        // ok is always true on device — the embedded fallback is non-empty
        // by construction (extra_targets.py enforces a non-placeholder for
        // prod and supplies the dev placeholder otherwise). Be defensive
        // anyway: leaving s_cachedKeyValid=false forces a retry next call
        // instead of handing out an all-zero key.
        if (!ok) {
            return nullptr;
        }
        s_cachedKeyValid = true;
    }
    if (outSource != nullptr) {
        *outSource = s_cachedSource;
    }
    return s_cachedKey;
}

namespace {

const char *keySourceName(KeySource src) {
    switch (src) {
        case KeySource::Nvs:
            return "NVS";
        case KeySource::NvsGenerated:
            return "NVS (generated)";
        case KeySource::Embedded:
            return "embedded (legacy)";
    }
    return "?";
}

} // namespace

void logBootKeyFingerprint() {
    static bool s_logged = false;
    if (s_logged) {
        return;
    }
    s_logged = true;

    KeySource src;
    const uint8_t *key = loadOrGenerateKey(&src);
    if (key == nullptr) {
        LOG_ERROR("OTA", "HMAC key load failed at boot — OTA will refuse uploads");
        return;
    }
    char fp[9] = {};
    if (!computeKeyFingerprint(key, kHmacLen, fp)) {
        LOG_WARN("OTA", "HMAC key sha-256 fingerprint compute failed");
        return;
    }
    // Format: source tag + 8-hex prefix of SHA-256(key). Embedded fingerprint
    // is identical across the fleet; NVS-derived fingerprints are unique per
    // device. Operators eyeballing logs can tell at a glance which key the
    // device is using.
    LOG_INFO("OTA", "HMAC key source=%s sha256=%s", keySourceName(src), fp);
}

bool computeKeyFingerprint(const uint8_t *key, size_t keyLen, char out[9]) {
    uint8_t digest[32];
    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    if (mbedtls_sha256_starts_ret(&ctx, /*is224=*/0) != 0) {
        mbedtls_sha256_free(&ctx);
        return false;
    }
    if (mbedtls_sha256_update_ret(&ctx, key, keyLen) != 0 ||
        mbedtls_sha256_finish_ret(&ctx, digest) != 0) {
        mbedtls_sha256_free(&ctx);
        return false;
    }
    mbedtls_sha256_free(&ctx);
    // First 4 bytes → 8 hex chars + NUL. Enough to detect drift between
    // devices without leaking exploitable bits of the key.
    static const char kHex[] = "0123456789abcdef";
    for (size_t i = 0; i < 4; ++i) {
        out[i * 2] = kHex[(digest[i] >> 4) & 0x0F];
        out[i * 2 + 1] = kHex[digest[i] & 0x0F];
    }
    out[8] = '\0';
    return true;
}

#endif // !UNIT_TEST

} // namespace OtaHmac
