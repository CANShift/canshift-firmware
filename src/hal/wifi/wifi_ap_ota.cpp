// wifi_ap_ota.cpp — HTTP server + OTA endpoints + dash-hosted Studio SPA.
//
// Split out of wifi_ap.cpp during the #1207 refactor. Owns:
//   - the Arduino WebServer instance (port 80) and its lifecycle accessors
//     (registerOtaRoutes / beginServer / handleClientTick / stopServer);
//   - the per-device OTA bearer token (#667), its derivation from the AP
//     password (SHA-256 || salt), and constant-time validation;
//   - the OTA HMAC verifier RAII storage (OtaVerifierStorage, #1207 #1314)
//     and the sticky failure flags surfaced by handleOtaComplete
//     (s_otaHmacOk, s_otaAuthRejected, s_otaUpdateBeginFailed — #1286);
//   - (under APP_SPA_SERVE) the dash-hosted Studio asset table and routes,
//     served from SPIFFS via WebServer::streamFile (#1077 phase 4, #1123).
//
// The orchestrator (wifi_ap.cpp) drives this module through the four
// lifecycle hooks declared in wifi_ap_internal.h. No public WifiAp:: symbol
// lives here.

#include "app_config.h"
#if APP_BLE_ENABLED

    #include "wifi_ap.h"
    #include "diag/logger.h"

    #if APP_WIFI_OTA_ENABLED

        #include "hal/wifi/ota_hmac.h"
        #include "hal/wifi/sw_sha256.h"
        #include "hal/wifi/wifi_ap_internal.h"

        #include <WiFi.h>
        #include <WebServer.h>
        #include <Update.h>
        #include <Arduino.h>
        #include <SPIFFS.h>
        #include <new>
        #include <stdio.h>
        #include <string.h>

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

static WebServer s_server(80);

// Per-request bearer token gating the /ota endpoint (issue #667).
//
// Derivation — deterministic from the AP password, so the token survives
// reboots and clients can recompute it from the password they already
// scanned/typed to join the softAP:
//
//     ota_token = first 16 bytes of SHA-256(ap_password || "ota-bearer-v1")
//
// Stored as a 32-char lowercase hex string (plus NUL). Clients send the
// header `Authorization: Bearer <hex>` on every POST /ota request; any
// mismatch returns HTTP 401 before a single byte of firmware is accepted.
static constexpr size_t OTA_TOKEN_BYTES = 16;
static constexpr size_t OTA_TOKEN_HEX_LEN = OTA_TOKEN_BYTES * 2;
static constexpr char OTA_TOKEN_SALT[] = "ota-bearer-v1";
static char s_otaTokenHex[OTA_TOKEN_HEX_LEN + 1] = {};

// OTA HMAC trailer verifier — placement-new'd into static storage for the
// duration of one upload so chunked WRITEs don't have to thread the verifier
// through closures. Static-lifetime storage means Update.begin() can never
// fail open because of a heap-exhaustion `new` on a fragmented device (#781).
// The HTTP server task is single-threaded, so a single static is safe.
//
// RAII helper (#1207 #1314): wraps the aligned storage + engaged flag so the
// placement-new / explicit-dtor dance lives in one place. Callers go through
// `construct()` / `destroy()` / `get()`; the destructor closes the slot at
// program shutdown if it was somehow still engaged. Cannot use std::optional
// because OtaHmacVerifier is non-movable.
namespace {
class OtaVerifierStorage {
  public:
    OtaVerifierStorage() = default;
    ~OtaVerifierStorage() {
        destroy();
    }
    OtaVerifierStorage(const OtaVerifierStorage &) = delete;
    OtaVerifierStorage &operator=(const OtaVerifierStorage &) = delete;

    template <typename... Args>
    OtaHmac::OtaHmacVerifier *construct(Args &&...args) {
        if (m_engaged) {
            destroy();
        }
        auto *p = new (m_storage) OtaHmac::OtaHmacVerifier(static_cast<Args &&>(args)...);
        m_engaged = true;
        return p;
    }

    void destroy() {
        if (!m_engaged) {
            return;
        }
        reinterpret_cast<OtaHmac::OtaHmacVerifier *>(&m_storage)->~OtaHmacVerifier();
        m_engaged = false;
    }

    OtaHmac::OtaHmacVerifier *get() {
        return m_engaged ? reinterpret_cast<OtaHmac::OtaHmacVerifier *>(&m_storage) : nullptr;
    }

  private:
    alignas(OtaHmac::OtaHmacVerifier) unsigned char m_storage[sizeof(OtaHmac::OtaHmacVerifier)];
    bool m_engaged = false;
};
} // namespace

static OtaVerifierStorage s_otaVerifierStorage;
static bool s_otaHmacOk = false;
// Sticky reject flag — set by the upload handler when the bearer token is
// missing or wrong so the matching complete handler can return 401 cleanly
// (Arduino WebServer dispatches upload + complete separately).
static bool s_otaAuthRejected = false;
// Sticky failure flag — set in the UPLOAD_FILE_START branch when
// Update.begin() fails (heap / partition). Surfaced as a distinct
// reason:"update_begin" in handleOtaComplete instead of being mis-attributed
// to HMAC. Issue #1286.
static bool s_otaUpdateBeginFailed = false;

// Dash-hosted Studio SPA — assets live on SPIFFS (#1123 follow-up).
//
// In #1077 phase 4 these files rode in flash via `board_build.embed_files`
// and were streamed from the linker `_binary_*_start` / `_end` symbols.
// That cost ~185 KB of the 1728 KB app slot — combined with the WiFi /
// WebServer / Update / WS libs the `_wifi` env overflowed at 107.3 %.
// Moving the SPA out of the firmware image into SPIFFS recovers the full
// 185 KB. The trade-off: a freshly-flashed dash now needs one extra
// `pio run -t uploadfs` step (or the equivalent canshift-flasher SPIFFS
// image flash) before the SPA loads — operational endpoints (`/status`,
// `/ota`) and BLE remain unaffected. The route table + handler live under
// `#if APP_SPA_SERVE` below.
//
// SPIFFS paths use the short prefix `/w/a/` (was `/web/assets/`) so the
// on-device filename stays under SPIFFS_OBJ_NAME_LEN (31 chars incl. NUL).
// Vite emits browser-side URLs at `/a/<name>` (matching the dist layout in
// `vite.config.ts`), so the route table maps `/a/<name>` URLs to
// `/w/a/<name>.gz` SPIFFS paths. The two prefixes deliberately match so
// the URL → SPIFFS translation is a single string concat at lookup time
// rather than a per-route hand-mapping table (#1240).

// ---------------------------------------------------------------------------
// HTTP handlers
// ---------------------------------------------------------------------------

namespace {

// Write the lowercase hex form of `len` bytes into `out` (out must be at
// least 2*len + 1 chars). Trailing NUL written. Local to keep this file
// self-contained — there's no shared hex helper in the codebase yet.
void bytesToHex(const uint8_t *bytes, size_t len, char *out) {
    static const char kHex[] = "0123456789abcdef";
    for (size_t i = 0; i < len; ++i) {
        out[i * 2] = kHex[(bytes[i] >> 4) & 0x0F];
        out[i * 2 + 1] = kHex[bytes[i] & 0x0F];
    }
    out[len * 2] = '\0';
}

// Constant-time compare for the 32-hex-char bearer token. The compared
// region is short and known, so we never branch on a per-byte match.
bool tokenMatches(const char *received) {
    if (received == nullptr || s_otaTokenHex[0] == '\0') {
        return false;
    }
    size_t len = 0;
    while (received[len] != '\0' && len < OTA_TOKEN_HEX_LEN + 1) {
        ++len;
    }
    if (len != OTA_TOKEN_HEX_LEN) {
        return false;
    }
    uint8_t diff = 0;
    for (size_t i = 0; i < OTA_TOKEN_HEX_LEN; ++i) {
        diff |= static_cast<uint8_t>(received[i] ^ s_otaTokenHex[i]);
    }
    return diff == 0;
}

// Validate the inbound `Authorization: Bearer <token>` header. Returns true
// on a clean match; returns false (and leaves no side effects) otherwise so
// the caller can decide whether to send 401 or just record a sticky reject.
bool hasValidBearerToken() {
    if (!s_server.hasHeader("Authorization")) {
        return false;
    }
    static const char kPrefix[] = "Bearer ";
    constexpr size_t kPrefixLen = sizeof(kPrefix) - 1;
    // Copy the header into a fixed-size C buffer so the OTA auth path doesn't
    // hold an Arduino String on the heap during Update.begin() (issue #782).
    // Sized for "Bearer " + 32 hex chars + NUL, with room for one extra byte
    // so an over-long header trips the length check below instead of being
    // silently truncated to a valid-looking value.
    char buf[kPrefixLen + OTA_TOKEN_HEX_LEN + 2] = {};
    strlcpy(buf, s_server.header("Authorization").c_str(), sizeof(buf));
    const size_t len = strlen(buf);
    if (len < kPrefixLen + OTA_TOKEN_HEX_LEN) {
        return false;
    }
    // Issue #1018 (SEC-M-4) — constant-time prefix compare. strncmp would
    // short-circuit on the first mismatching byte and leak (via timing) which
    // byte of the "Bearer " literal first diverged. The prefix is fixed-length
    // and known, so reuse the same accumulator-OR helper used for the token
    // compare below.
    if (OtaHmac::constantTimeMemcmp(reinterpret_cast<const uint8_t *>(buf),
                                    reinterpret_cast<const uint8_t *>(kPrefix), kPrefixLen) != 0) {
        return false;
    }
    return tokenMatches(buf + kPrefixLen);
}

void handleStatus() {
    char buf[64];
    snprintf(buf, sizeof(buf), "{\"status\":\"ok\",\"ver\":\"%s\"}", APP_VERSION_STR);
    s_server.send(200, "application/json", buf);
}

        #if APP_SPA_SERVE

// ---------------------------------------------------------------------------
// Dash-hosted Studio SPA — static asset serving (#1077 phase 4, #1123 follow-up)
// ---------------------------------------------------------------------------
//
// Browser-fetched artifacts live on SPIFFS at `/w/...` and stream straight
// from the file system via WebServer::streamFile, which auto-sends the
// Content-Encoding: gzip header for any filename ending in `.gz` (see
// _streamFileCore in arduino-esp32 WebServer.cpp). The route table maps each
// URL the SPA can request to its SPIFFS path + Content-Type. Short paths
// keep mkspiffs from silently dropping any chunk over SPIFFS_OBJ_NAME_LEN
// (31 chars, see #1240).

struct SpaAsset {
    const char *urlPath;     // URL path the browser requests
    const char *spiffsPath;  // SPIFFS path the asset lives at
    const char *contentType; // MIME for the Content-Type header
};

// Stream a single SPA asset from SPIFFS. WebServer::streamFile reads in
// fixed-size chunks (1436 B by default), so even the 58 KB vendor-react bundle
// never blows up the heap. The .gz suffix on the SPIFFS path triggers the
// framework's automatic Content-Encoding: gzip header, so we don't set it
// manually here.
void serveSpaAsset(const SpaAsset &asset) {
    File file = SPIFFS.open(asset.spiffsPath, "r");
    if (!file || file.isDirectory()) {
        LOG_WARN("WiFi", "SPA asset missing on SPIFFS: %s", asset.spiffsPath);
        s_server.send(404, "text/plain", "SPA asset not provisioned (run uploadfs)");
        if (file) {
            file.close();
        }
        return;
    }
    s_server.sendHeader("Cache-Control", "no-store");
    s_server.streamFile(file, asset.contentType);
    file.close();
}

// Single table — every URL the SPA can request. Index 0 is the SPA shell
// (served at both `/` and `/index.html`); the rest are referenced by the
// HTML / CSS the browser parses after that. New chunks → new row here AND
// a matching entry in scripts/sync_studio_web.py. SPIFFS-resident; see
// `pio run -t uploadfs` in the README first-flash section.
//
// Path lengths — the SPIFFS column stays under SPIFFS_OBJ_NAME_LEN (31 chars
// incl. NUL); the longest entry is `/w/a/Orbitron-Medium.woff2` at 26 chars.
// The sync_studio_web.py preflight asserts the same invariant at build time
// so a future asset rename can't silently regress past the cap (#1240).
const SpaAsset kSpaAssets[] = {
    {"/index.html", "/w/index.html.gz", "text/html"},
    {"/a/index.js", "/w/a/index.js.gz", "application/javascript"},
    {"/a/index.css", "/w/a/index.css.gz", "text/css"},
    {"/a/vendor-react.js", "/w/a/vendor-react.js.gz", "application/javascript"},
    {"/a/vendor-radix.js", "/w/a/vendor-radix.js.gz", "application/javascript"},
    {"/a/vendor-state.js", "/w/a/vendor-state.js.gz", "application/javascript"},
    {"/a/EditorRoute.js", "/w/a/EditorRoute.js.gz", "application/javascript"},
    {"/a/Orbitron-Black.woff2", "/w/a/Orbitron-Black.woff2", "font/woff2"},
    {"/a/Orbitron-Bold.woff2", "/w/a/Orbitron-Bold.woff2", "font/woff2"},
    {"/a/Orbitron-Medium.woff2", "/w/a/Orbitron-Medium.woff2", "font/woff2"},
};
constexpr size_t kSpaAssetCount = sizeof(kSpaAssets) / sizeof(kSpaAssets[0]);

void registerSpaRoutes() {
    // Root path → shell HTML (same SPIFFS path as /index.html). Kept as a
    // separate registration so the WebServer's exact-match dispatcher returns
    // the SPA when the user types `http://canshift.local/` with no path.
    s_server.on("/", HTTP_GET, []() { serveSpaAsset(kSpaAssets[0]); });
    for (size_t i = 0; i < kSpaAssetCount; ++i) {
        const SpaAsset &asset = kSpaAssets[i];
        s_server.on(asset.urlPath, HTTP_GET, [&asset]() { serveSpaAsset(asset); });
    }
}

        #endif // APP_SPA_SERVE

void cleanupVerifier() {
    s_otaVerifierStorage.destroy();
}

// Sink invoked by the HMAC verifier with body bytes (everything but the
// trailing 32-byte HMAC). Returning false aborts the upload.
bool otaUpdateSink(const uint8_t *data, size_t len, void * /*user*/) {
    if (Update.write(const_cast<uint8_t *>(data), len) != len) {
        LOG_ERROR("WiFi", "Update.write mismatch");
        return false;
    }
    return true;
}

void handleOtaComplete() {
    // Auth gate takes precedence: a 401 leaves no ambiguity about why the
    // upload was rejected and avoids leaking HMAC / Update state to an
    // unauthenticated peer.
    if (s_otaAuthRejected) {
        s_server.send(401, "application/json", "{\"status\":\"error\",\"reason\":\"auth\"}");
        s_otaAuthRejected = false;
        cleanupVerifier();
        Update.abort();
        LOG_WARN("WiFi", "OTA rejected: missing or invalid bearer token");
        return;
    }

    // Reject if either the Update layer reported an error OR the HMAC check
    // failed (or never happened when it was required). The order below picks
    // the failure cause so we can return an accurate status code + reason:
    //   - Update.begin() failure (heap / partition) takes precedence and is
    //     reported as 500 with reason "update_begin" (#1286).
    //   - HMAC mismatch on a peer-uploaded image is a peer-side failure and
    //     maps to HTTP 403, not 500 (#1286).
    //   - Other Update.* failures (write / end / I/O) stay as 500.
    const bool updateOk = !Update.hasError();
    const bool hmacRequired = (APP_OTA_REQUIRE_HMAC != 0);
    const bool hmacOk = hmacRequired ? s_otaHmacOk : true;
    const bool ok = updateOk && hmacOk && !s_otaUpdateBeginFailed;

    const char *body = nullptr;
    int statusCode = 200;
    if (ok) {
        body = "{\"status\":\"ok\"}";
        statusCode = 200;
    } else if (s_otaUpdateBeginFailed) {
        body = "{\"status\":\"error\",\"reason\":\"update_begin\"}";
        statusCode = 500;
    } else if (!hmacOk) {
        body = "{\"status\":\"error\",\"reason\":\"hmac\"}";
        statusCode = 403;
    } else {
        body = "{\"status\":\"error\"}";
        statusCode = 500;
    }
    s_server.send(statusCode, "application/json", body);

    cleanupVerifier();
    const bool beginFailed = s_otaUpdateBeginFailed;
    s_otaUpdateBeginFailed = false;

    if (ok) {
        LOG_INFO("WiFi", "OTA complete — rebooting");
        delay(PRE_RESTART_FLUSH_DELAY_MS);
        esp_restart();
    } else if (beginFailed) {
        LOG_ERROR("WiFi", "OTA failed: Update.begin (heap / partition)");
    } else if (!hmacOk) {
        LOG_ERROR("WiFi", "OTA rejected: HMAC verification failed");
    } else {
        LOG_ERROR("WiFi", "OTA failed: %s", Update.errorString());
    }
}

void handleOtaUpload() {
    HTTPUpload &upload = s_server.upload();
    if (upload.status == UPLOAD_FILE_START) {
        // Bearer-token check happens on the first chunk so the upload handler
        // doesn't even invoke Update.begin() for unauthenticated peers
        // (issue #667). The Arduino WebServer calls this handler before the
        // matching complete handler, so we record the rejection in a sticky
        // flag and let handleOtaComplete() send the 401.
        if (!hasValidBearerToken()) {
            s_otaAuthRejected = true;
            return;
        }
        s_otaAuthRejected = false;

        LOG_INFO("WiFi", "OTA upload start: %s (%u bytes expected)", upload.filename.c_str(),
                 upload.totalSize);
        s_otaHmacOk = false;
        // Reset the sticky begin-failure flag at the start of every fresh
        // upload so a previous attempt's outcome doesn't bleed through (#1286).
        s_otaUpdateBeginFailed = false;
        cleanupVerifier();

        if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
            LOG_ERROR("WiFi", "Update.begin failed: %s", Update.errorString());
            // Mark the upload as begin-failed so handleOtaComplete returns
            // reason:"update_begin" instead of mis-attributing to HMAC. The
            // subsequent UPLOAD_FILE_WRITE chunks bypass the (null) verifier
            // and the matching complete handler surfaces the correct cause.
            s_otaUpdateBeginFailed = true;
            return;
        }

        #if APP_OTA_REQUIRE_HMAC
        // Per-device key (issue #521): pulled from NVS namespace `ota`, key
        // `hmac_key`. Generated on first boot or read on subsequent boots;
        // falls through to the embedded OTA_HMAC_SECRET on NVS write failure
        // so legacy installs keep verifying through the rollout window.
        const uint8_t *kSecret = OtaHmac::loadOrGenerateKey();
        if (kSecret == nullptr) {
            LOG_ERROR("WiFi", "OTA HMAC key load failed");
            Update.abort();
            return;
        }
        auto *verifier = s_otaVerifierStorage.construct(OtaHmac::mbedtlsHmacBackend(), kSecret,
                                                        OtaHmac::kHmacLen, otaUpdateSink, nullptr);
        if (!verifier->begin()) {
            LOG_ERROR("WiFi", "OTA HMAC verifier init failed");
            cleanupVerifier();
            Update.abort();
        }
        #else
        LOG_WARN("WiFi", "OTA HMAC verification disabled (APP_OTA_REQUIRE_HMAC=0) — insecure");
        #endif
    } else if (upload.status == UPLOAD_FILE_WRITE) {
        if (s_otaAuthRejected) {
            return; // unauthenticated peer — drop the bytes on the floor
        }
        #if APP_OTA_REQUIRE_HMAC
        auto *verifier = s_otaVerifierStorage.get();
        if (verifier == nullptr) {
            return; // begin failed earlier
        }
        if (!verifier->feed(upload.buf, upload.currentSize)) {
            LOG_ERROR("WiFi", "OTA upload feed failed");
            cleanupVerifier();
            Update.abort();
        }
        #else
        if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
            LOG_ERROR("WiFi", "Update.write mismatch");
        }
        #endif
    } else if (upload.status == UPLOAD_FILE_END) {
        if (s_otaAuthRejected) {
            return; // handled by handleOtaComplete → 401
        }
        #if APP_OTA_REQUIRE_HMAC
        auto *verifier = s_otaVerifierStorage.get();
        if (verifier == nullptr) {
            return;
        }
        const bool hmacMatch = verifier->finish();
        s_otaHmacOk = hmacMatch;
        if (!hmacMatch) {
            LOG_ERROR("WiFi", "OTA HMAC mismatch — aborting");
            Update.abort();
            return;
        }
        #else
        s_otaHmacOk = true; // unchecked, but the gate below ignores it
        #endif
        if (Update.end(true)) {
            LOG_INFO("WiFi", "OTA upload done: %u bytes written", upload.totalSize);
        } else {
            LOG_ERROR("WiFi", "Update.end failed: %s", Update.errorString());
        }
    } else if (upload.status == UPLOAD_FILE_ABORTED) {
        LOG_WARN("WiFi", "OTA upload aborted by client");
        cleanupVerifier();
        Update.abort();
    }
}

} // namespace

// ---------------------------------------------------------------------------
// Cross-module lifecycle hooks — called by wifi_ap.cpp::apTaskFn.
// ---------------------------------------------------------------------------

// Compute the per-device OTA bearer token from the live AP password and the
// versioned salt. Called once at AP start; result cached in s_otaTokenHex.
// Returns false only if the AP password isn't loaded yet (effectively
// impossible past WifiAp::start() which always calls ensurePassword).
bool WifiApInternal::deriveOtaToken() {
    const char *password = WifiApInternal::apPassword();
    if (password == nullptr || password[0] == '\0') {
        return false;
    }
    // SW SHA-256 — same race avoidance as ota_hmac.cpp fingerprint path. The
    // ESP32 HW SHA engine asserts when invoked while another stack (BLE host
    // at runtime, or even partially-initialised WiFi crypto) holds the lock.
    uint8_t digest[32];
    canshift::hal::wifi::SwSha256Ctx ctx;
    canshift::hal::wifi::sw_sha256_init(&ctx);
    canshift::hal::wifi::sw_sha256_update(&ctx, reinterpret_cast<const uint8_t *>(password),
                                          strlen(password));
    canshift::hal::wifi::sw_sha256_update(&ctx, reinterpret_cast<const uint8_t *>(OTA_TOKEN_SALT),
                                          sizeof(OTA_TOKEN_SALT) - 1);
    canshift::hal::wifi::sw_sha256_final(&ctx, digest);
    bytesToHex(digest, OTA_TOKEN_BYTES, s_otaTokenHex);
    return true;
}

void WifiApInternal::registerOtaRoutes() {
    // Tell the WebServer parser to retain the Authorization header on each
    // request. Arduino WebServer drops every header except this allow-list,
    // so without this hasHeader("Authorization") would always return false.
    static const char *kCollect[] = {"Authorization"};
    s_server.collectHeaders(kCollect, sizeof(kCollect) / sizeof(kCollect[0]));

    s_server.on("/status", HTTP_GET, handleStatus);
    // /ota — firmware upload endpoint (issue #667).
    //
    // Clients MUST authenticate with `Authorization: Bearer <hex>` where
    // <hex> is the 32-char lowercase token derived from the AP password:
    //     ota_token = first 16 bytes of SHA-256(ap_password || "ota-bearer-v1")
    // The studio and the mobile app both ship a matching derivation in
    // canshift-mobile/src/services/ota-secret.ts and the studio's OTA push
    // pipeline; keep all three in sync.
    s_server.on("/ota", HTTP_POST, handleOtaComplete, handleOtaUpload);

        #if APP_SPA_SERVE
    // Dash-hosted Studio SPA routes — `/`, `/index.html`, `/a/*`
    // (#1077 phase 4). Registered after the operational endpoints so the
    // exact-match dispatcher tries `/status` / `/ota` first.
    registerSpaRoutes();
        #endif
}

void WifiApInternal::beginServer() {
    s_server.begin();
}

void WifiApInternal::handleClientTick() {
    s_server.handleClient();
}

void WifiApInternal::stopServer() {
    s_server.stop();
}

    #endif // APP_WIFI_OTA_ENABLED

#endif // APP_BLE_ENABLED
