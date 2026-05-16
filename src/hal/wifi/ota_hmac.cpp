// ota_hmac.cpp — rolling-window HMAC verifier for OTA uploads.
//
// See ota_hmac.h for the trailer format and design rationale. The streaming
// logic here is platform-agnostic and exercised by host unit tests through a
// stub backend; the mbedTLS backend at the bottom of this file is the only
// part that needs the ESP-IDF.

#include "hal/wifi/ota_hmac.h"

#include <string.h>

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
// Production backend — mbedTLS. Excluded from native unit tests, which use a
// stub backend that exercises only the framing/streaming logic above.
// ---------------------------------------------------------------------------
#ifndef UNIT_TEST

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

#endif // !UNIT_TEST

} // namespace OtaHmac
