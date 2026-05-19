// ota_hmac_bridge.cpp — Rust-backed implementation of `mbedtlsHmacBackend()`.
//
// Compiled only when `USE_RUST_OTA_HMAC=1` is in build_flags (#827 Phase 3).
// The default build keeps using the mbedTLS path in `ota_hmac.cpp` —
// switching to Rust is a one-flag flip once device-side validation is done.
//
// Design: plug the Rust crate's *raw HMAC primitive* into the existing
// `OtaHmac::HmacBackend` interface. The C++ streaming layer
// (`OtaHmacVerifier`) is untouched — same rolling-window framing, same
// 24-Unity-test coverage, just a different HMAC-SHA256 implementation
// underneath. Algorithm parity with mbedTLS is byte-identical (both
// implement RFC 2104 with SHA-256).
//
// Storage: a single static `OtaHmacRsRaw` slot — single-instance OTA
// matches the mbedTLS backend's `s_mdContextInUse` guard. No heap alloc.

#include "app_config.h"
#if APP_WIFI_OTA_ENABLED && USE_RUST_OTA_HMAC

    #include "hal/wifi/ota_hmac.h"
    #include "ota_hmac_rs.h"

    #include <string.h>

namespace OtaHmac {

namespace {

// 64-byte scratch storage with 8-byte alignment is enough headroom for
// every plausible layout `OtaHmacRsRaw` will ever take (the inner state
// is a `Option<RustCryptoBackend>` which wraps `Option<Hmac<Sha256>>`).
// `rustInit` asserts `ota_hmac_rs_raw_sizeof()` fits — bump if a Rust
// crate version reports a larger size.
alignas(8) uint8_t s_rawSlot[128];
bool s_rawInUse = false;

void *rustInit(const uint8_t *secret, size_t secretLen) {
    if (s_rawInUse) {
        return nullptr;
    }
    if (ota_hmac_rs_raw_sizeof() > sizeof(s_rawSlot)) {
        return nullptr;
    }
    if (ota_hmac_rs_raw_alignof() > alignof(decltype(s_rawSlot))) {
        return nullptr;
    }
    memset(s_rawSlot, 0, sizeof(s_rawSlot));

    auto *slot = reinterpret_cast<OtaHmacRsRaw *>(s_rawSlot);
    if (ota_hmac_rs_raw_init(slot, secret, secretLen) != 0) {
        return nullptr;
    }
    s_rawInUse = true;
    return slot;
}

bool rustUpdate(void *ctx, const uint8_t *data, size_t len) {
    auto *slot = static_cast<OtaHmacRsRaw *>(ctx);
    return ota_hmac_rs_raw_update(slot, data, len) == 0;
}

bool rustFinalize(void *ctx, uint8_t out[kHmacLen]) {
    auto *slot = static_cast<OtaHmacRsRaw *>(ctx);
    const int32_t rc = ota_hmac_rs_raw_finalize(slot, out);
    s_rawInUse = false;
    return rc == 0;
}

const HmacBackend kRustBackend = {rustInit, rustUpdate, rustFinalize};

} // namespace

// Override the mbedTLS backend selector. The mbedTLS implementation is
// still in ota_hmac.cpp (gated `#ifndef UNIT_TEST`); when USE_RUST_OTA_HMAC
// is on, this TU's `mbedtlsHmacBackend()` definition wins at link time and
// the OTA verifier picks up the Rust backend.
//
// NOTE: the C++ compiler-flag gate makes the ota_hmac.cpp version
// disappear when USE_RUST_OTA_HMAC=1; both definitions would collide
// otherwise. See the matching `#ifndef USE_RUST_OTA_HMAC` block at the
// bottom of ota_hmac.cpp.
const HmacBackend &mbedtlsHmacBackend() {
    return kRustBackend;
}

} // namespace OtaHmac

#endif // APP_WIFI_OTA_ENABLED && USE_RUST_OTA_HMAC
