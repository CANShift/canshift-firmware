// wifi_ap.cpp — WiFi AP mode + HTTP OTA server
//
// Behind APP_WIFI_OTA_ENABLED so the Arduino WiFi / WebServer / Update libs
// stay out of the link when not needed (~80 KB flash). When disabled, the
// WifiAp:: API resolves to no-op stubs so BLE callers don't need to know.

#include "app_config.h"
#if APP_BLE_ENABLED

    #include "wifi_ap.h"
    #include "diag/logger.h"

    #if APP_WIFI_OTA_ENABLED

        #include "hal/wifi/ota_hmac.h"
        #include "hal/wifi/wifi_tcp.h"
        #include "hal/wifi/wifi_ws.h"

        #include <WiFi.h>
        #include <WebServer.h>
        #include <Update.h>
        #include <Arduino.h>
        #include <ESPmDNS.h>
        #include <Preferences.h>
        #include <esp_system.h>
        #include <esp_task_wdt.h>
        #include <freertos/FreeRTOS.h>
        #include <freertos/task.h>
        #include <mbedtls/sha256.h>
        #include <new>
        #include <stdio.h>
        #include <string.h>

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

static WebServer s_server(80);
static TaskHandle_t s_taskHandle = nullptr;
static volatile bool s_active = false;
static char s_ssid[20] = {};

// Per-device AP password — 32 hex chars + null terminator. Generated on first
// boot via esp_fill_random() (128 bits entropy) and persisted in NVS. Issue
// #910 raised this from the previous 64-bit (16-char) password: WPA2 4-way
// handshake capture within the 5-minute AP window followed by offline brute
// force at ~10^10 H/s is plausible against 64 bits; 128 bits keeps brute
// force infeasible at any realistic hardware budget.
static constexpr size_t AP_PASSWORD_LEN = 32;
static constexpr size_t AP_PASSWORD_ENTROPY_BYTES = 16; // 128 bits, hex-encoded
static char s_password[AP_PASSWORD_LEN + 1] = {};

static constexpr char NVS_NS_WIFI_AP[] = "wifi_ap";
static constexpr char NVS_KEY_PWD[] = "pwd";

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
alignas(OtaHmac::OtaHmacVerifier) static unsigned char s_otaVerifierStorage[sizeof(
    OtaHmac::OtaHmacVerifier)];
static OtaHmac::OtaHmacVerifier *s_otaVerifier = nullptr;
static bool s_otaHmacOk = false;
// Sticky reject flag — set by the upload handler when the bearer token is
// missing or wrong so the matching complete handler can return 401 cleanly
// (Arduino WebServer dispatches upload + complete separately).
static bool s_otaAuthRejected = false;

        #if APP_SPA_SERVE
// Dash-hosted Studio SPA — embed-file symbol declarations (#1077 phase 4).
// PlatformIO's `board_build.embed_files` exposes each blob as a pair of
// linker symbols `_binary_<munged_path>_start` / `_end`. We list every SPA
// artifact here at file scope (extern "C" + asm-label naming) so the
// anonymous-namespace route table below can take their addresses; declaring
// these inside the namespace would prevent C linkage and the linker would
// emit undefined-symbol errors.

extern "C" {

            #define EMBED_BLOB(name)                                                               \
                extern const uint8_t name##_start[] asm("_binary_" #name "_start");                \
                extern const uint8_t name##_end[] asm("_binary_" #name "_end")

EMBED_BLOB(data_web_index_html_gz);
EMBED_BLOB(data_web_assets_index_js_gz);
EMBED_BLOB(data_web_assets_index_css_gz);
EMBED_BLOB(data_web_assets_vendor_react_js_gz);
EMBED_BLOB(data_web_assets_vendor_radix_js_gz);
EMBED_BLOB(data_web_assets_vendor_state_js_gz);
EMBED_BLOB(data_web_assets_EditorRoute_js_gz);
EMBED_BLOB(data_web_assets_Orbitron_Black_woff2);
EMBED_BLOB(data_web_assets_Orbitron_Bold_woff2);
EMBED_BLOB(data_web_assets_Orbitron_Medium_woff2);

            #undef EMBED_BLOB

} // extern "C"
        #endif // APP_SPA_SERVE

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

// Compute the per-device OTA bearer token from the live AP password and the
// versioned salt. Called once at AP start; result cached in s_otaTokenHex.
// Returns false only if mbedTLS fails to initialise (effectively impossible
// on ESP32 IDF builds but checked anyway so we don't ship an empty token).
bool deriveOtaToken() {
    if (s_password[0] == '\0') {
        return false;
    }
    uint8_t digest[32];
    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    if (mbedtls_sha256_starts_ret(&ctx, /*is224=*/0) != 0) {
        mbedtls_sha256_free(&ctx);
        return false;
    }
    if (mbedtls_sha256_update_ret(&ctx, reinterpret_cast<const uint8_t *>(s_password),
                                  strlen(s_password)) != 0 ||
        mbedtls_sha256_update_ret(&ctx, reinterpret_cast<const uint8_t *>(OTA_TOKEN_SALT),
                                  sizeof(OTA_TOKEN_SALT) - 1) != 0 ||
        mbedtls_sha256_finish_ret(&ctx, digest) != 0) {
        mbedtls_sha256_free(&ctx);
        return false;
    }
    mbedtls_sha256_free(&ctx);
    bytesToHex(digest, OTA_TOKEN_BYTES, s_otaTokenHex);
    return true;
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
// Dash-hosted Studio SPA — static asset serving (#1077 phase 4)
// ---------------------------------------------------------------------------
//
// Browser-fetched artifacts live in flash via `board_build.embed_files`;
// extern "C" declarations sit at file scope (see top of this file). The
// table below maps each URL the SPA can request to its embed-symbol pair.

struct SpaAsset {
    const char *path;        // URL path the browser requests
    const uint8_t *start;    // first byte of the embedded blob
    const uint8_t *end;      // one-past-last byte of the embedded blob
    const char *contentType; // MIME for the Content-Type header
    bool gzipped;            // sets Content-Encoding: gzip when true
};

// Send a single embedded asset. send_P() streams from flash without an
// intermediate heap copy — important on the dash because index.js gzipped is
// ~28 KB and a String copy of that on a fragmented heap would risk a
// fragmentation-induced fail mid-handler. Cache-Control: no-store because
// the AP isn't a CDN and the next OTA may rewrite the bytes.
void serveSpaAsset(const SpaAsset &asset) {
    const size_t len = static_cast<size_t>(asset.end - asset.start);
    if (asset.gzipped) {
        s_server.sendHeader("Content-Encoding", "gzip");
    }
    s_server.sendHeader("Cache-Control", "no-store");
    s_server.send_P(200, asset.contentType, reinterpret_cast<const char *>(asset.start), len);
}

// Single table — every URL the SPA can request. Index 0 is the SPA shell
// (served at both `/` and `/index.html`); the rest are referenced by the
// HTML / CSS the browser parses after that. New chunks → new row here AND
// a matching EMBED_BLOB() at the top of this file AND a matching entry in
// scripts/sync_studio_web.py + platformio.ini.
const SpaAsset kSpaAssets[] = {
    {"/index.html", data_web_index_html_gz_start, data_web_index_html_gz_end, "text/html", true},
    {"/assets/index.js", data_web_assets_index_js_gz_start, data_web_assets_index_js_gz_end,
     "application/javascript", true},
    {"/assets/index.css", data_web_assets_index_css_gz_start, data_web_assets_index_css_gz_end,
     "text/css", true},
    {"/assets/vendor-react.js", data_web_assets_vendor_react_js_gz_start,
     data_web_assets_vendor_react_js_gz_end, "application/javascript", true},
    {"/assets/vendor-radix.js", data_web_assets_vendor_radix_js_gz_start,
     data_web_assets_vendor_radix_js_gz_end, "application/javascript", true},
    {"/assets/vendor-state.js", data_web_assets_vendor_state_js_gz_start,
     data_web_assets_vendor_state_js_gz_end, "application/javascript", true},
    {"/assets/EditorRoute.js", data_web_assets_EditorRoute_js_gz_start,
     data_web_assets_EditorRoute_js_gz_end, "application/javascript", true},
    {"/assets/Orbitron-Black.woff2", data_web_assets_Orbitron_Black_woff2_start,
     data_web_assets_Orbitron_Black_woff2_end, "font/woff2", false},
    {"/assets/Orbitron-Bold.woff2", data_web_assets_Orbitron_Bold_woff2_start,
     data_web_assets_Orbitron_Bold_woff2_end, "font/woff2", false},
    {"/assets/Orbitron-Medium.woff2", data_web_assets_Orbitron_Medium_woff2_start,
     data_web_assets_Orbitron_Medium_woff2_end, "font/woff2", false},
};
constexpr size_t kSpaAssetCount = sizeof(kSpaAssets) / sizeof(kSpaAssets[0]);

void registerSpaRoutes() {
    // Root path → shell HTML (same blob as /index.html). Kept as a separate
    // registration so the WebServer's exact-match dispatcher returns the SPA
    // when the user types `http://canshift.local/` with no path.
    s_server.on("/", HTTP_GET, []() { serveSpaAsset(kSpaAssets[0]); });
    for (size_t i = 0; i < kSpaAssetCount; ++i) {
        const SpaAsset &asset = kSpaAssets[i];
        s_server.on(asset.path, HTTP_GET, [&asset]() { serveSpaAsset(asset); });
    }
}

        #endif // APP_SPA_SERVE

void cleanupVerifier() {
    if (s_otaVerifier != nullptr) {
        s_otaVerifier->~OtaHmacVerifier();
        s_otaVerifier = nullptr;
    }
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
    // failed (or never happened when it was required).
    const bool updateOk = !Update.hasError();
    const bool hmacRequired = (APP_OTA_REQUIRE_HMAC != 0);
    const bool hmacOk = hmacRequired ? s_otaHmacOk : true;
    const bool ok = updateOk && hmacOk;

    const char *body = nullptr;
    if (ok) {
        body = "{\"status\":\"ok\"}";
    } else if (!hmacOk) {
        body = "{\"status\":\"error\",\"reason\":\"hmac\"}";
    } else {
        body = "{\"status\":\"error\"}";
    }
    s_server.send(ok ? 200 : 500, "application/json", body);

    cleanupVerifier();

    if (ok) {
        LOG_INFO("WiFi", "OTA complete — rebooting");
        delay(PRE_RESTART_FLUSH_DELAY_MS);
        esp_restart();
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
        cleanupVerifier();

        if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
            LOG_ERROR("WiFi", "Update.begin failed: %s", Update.errorString());
            return;
        }

        #if APP_OTA_REQUIRE_HMAC
        static const char kSecret[] = OTA_HMAC_SECRET;
        s_otaVerifier = new (s_otaVerifierStorage) OtaHmac::OtaHmacVerifier(
            OtaHmac::mbedtlsHmacBackend(), reinterpret_cast<const uint8_t *>(kSecret),
            sizeof(kSecret) - 1, // exclude trailing NUL
            otaUpdateSink, nullptr);
        if (!s_otaVerifier->begin()) {
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
        if (s_otaVerifier == nullptr) {
            return; // begin failed earlier
        }
        if (!s_otaVerifier->feed(upload.buf, upload.currentSize)) {
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
        if (s_otaVerifier == nullptr) {
            return;
        }
        const bool hmacMatch = s_otaVerifier->finish();
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

void buildSsid() {
    uint8_t mac[6];
    WiFi.macAddress(mac);
    snprintf(s_ssid, sizeof(s_ssid), "CANShift-%02X%02X", mac[4], mac[5]);
}

void ensurePassword() {
    if (s_password[0] != '\0')
        return; // already loaded this boot

    Preferences p;
    if (p.begin(NVS_NS_WIFI_AP, /*readOnly=*/true)) {
        const size_t len = p.getString(NVS_KEY_PWD, s_password, sizeof(s_password));
        p.end();
        if (len == AP_PASSWORD_LEN)
            return; // valid persisted value
    }

    // Missing, empty, or wrong length — generate a fresh 128-bit random
    // password. `esp_fill_random` populates the buffer from the hardware
    // RNG in a single call (cheaper than two `esp_random()` calls and avoids
    // the per-word join). Hex-encoded for a 32-char ASCII string.
    uint8_t entropy[AP_PASSWORD_ENTROPY_BYTES];
    esp_fill_random(entropy, sizeof(entropy));
    for (size_t i = 0; i < AP_PASSWORD_ENTROPY_BYTES; ++i) {
        snprintf(&s_password[i * 2], 3, "%02x", entropy[i]);
    }

    Preferences pw;
    if (pw.begin(NVS_NS_WIFI_AP, /*readOnly=*/false)) {
        pw.putString(NVS_KEY_PWD, s_password);
        pw.end();
        LOG_INFO("WiFi", "Generated new AP password (persisted in NVS)");
    } else {
        LOG_WARN("WiFi", "NVS open failed — using volatile AP password");
    }
}

void apTaskFn(void *) {
        // Issue #1006 — subscribe this task to the WDT armed in BootSequence.
        // The task is created on-demand from WifiAp::start() and self-deletes
        // after BLE_WIFI_AP_TIMEOUT_MS or an explicit stop. Subscribe from
        // inside the task body (pass nullptr = current task) so the handle slot
        // is guaranteed live when esp_task_wdt_add runs.
        //
        // Skipped in simulation builds — the WDT is itself disabled there
        // (boot_sequence.cpp) and esp_task_wdt_add would return ESP_ERR_INVALID_STATE.
        #if !APP_SIMULATION_MODE
    const esp_err_t wdtAddErr = esp_task_wdt_add(nullptr);
    if (wdtAddErr != ESP_OK) {
        LOG_WARN("WiFi", "WDT add(wifi_ap) failed: %d", static_cast<int>(wdtAddErr));
    }
        #endif

    WiFi.softAP(s_ssid, s_password);
    LOG_INFO("WiFi", "AP started — SSID: %s  IP: %s", s_ssid, WiFi.softAPIP().toString().c_str());

    // mDNS responder — exposes the dash as `canshift.local` and advertises
    // the JSON-lines TCP server as `_canshift._tcp` so Studio can discover
    // and connect without a manual IP. Failure is non-fatal: the AP keeps
    // serving and a manual IP fallback still works. Issue #1071.
    //
    // The WebSocket transport (#1105) ships as a sibling `_canshift_ws._tcp`
    // service on port 81 because the chosen WS library opens its own
    // listening socket rather than sharing port 80 with the OTA WebServer.
    // The `path=/` TXT record disambiguates from the TCP service for
    // dash-hosted Studio which selects on path.
    if (MDNS.begin("canshift")) {
        MDNS.addService("canshift", "tcp", 5050);
        MDNS.addService("canshift_ws", "tcp", 81);
        MDNS.addServiceTxt("canshift_ws", "tcp", "path", "/");
        LOG_INFO("WiFi", "mDNS up: canshift.local  services: _canshift._tcp:5050 "
                         "_canshift_ws._tcp:81");
    } else {
        LOG_WARN("WiFi", "mDNS.begin() failed — Studio discovery disabled, manual IP only");
    }

    // TCP server for Studio JSON-lines transport (issue #1071). Starts after
    // the AP is live so the listening socket binds to the softAP interface.
    WifiTcpServer::start();

    // WebSocket server for browser-based dash-hosted Studio (issue #1105).
    // Mirrors the JSON-lines protocol over WS text frames on port 81. Single
    // client; coexists with the TCP server above (last-connect wins the aux
    // sink slot for telemetry fan-out).
    WifiWsServer::start();

    // Derive the per-device OTA bearer token once per AP session. Cheap
    // (one SHA-256) and deterministic, so the token stays stable across
    // reboots as long as the AP password in NVS doesn't rotate.
    if (!deriveOtaToken()) {
        LOG_ERROR("WiFi", "OTA bearer token derivation failed — /ota will reject everything");
    }

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
    // Dash-hosted Studio SPA routes — `/`, `/index.html`, `/assets/*`
    // (#1077 phase 4). Registered after the operational endpoints so the
    // exact-match dispatcher tries `/status` / `/ota` first.
    registerSpaRoutes();
        #endif

    s_server.begin();

    const uint32_t startMs = millis();
    while (s_active && (millis() - startMs < BLE_WIFI_AP_TIMEOUT_MS)) {
        s_server.handleClient();

        // Issue #1006 — WiFi AP task WDT feed. Placed AFTER handleClient()
        // so a real hang inside the OTA upload / Update.end() path still
        // trips the watchdog. The 10 ms vTaskDelay cadence + Update.end()'s
        // worst-case ~2 s erase-and-finalise stays comfortably below
        // TASK_WDT_TIMEOUT_MS (8 s).
        #if !APP_SIMULATION_MODE
        esp_task_wdt_reset();
        #endif
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    // Tear down the WS server first (issue #1105) and then the TCP server
    // (issue #1071) before dropping the AP so each listener socket releases
    // its port and any connected Studio sees a clean CLOSE / FIN rather than
    // a half-open connection. WS goes first because it shares the same
    // Arduino-WiFi stack as TCP and its teardown is slightly slower (the
    // library walks WEBSOCKETS_SERVER_CLIENT_MAX slots).
    WifiWsServer::stop();
    WifiTcpServer::stop();
    MDNS.end();

    s_server.stop();
    WiFi.softAPdisconnect(true);

        // Unregister BEFORE self-delete: a deleted task that's still on the WDT
        // registry triggers the panic handler on the next watchdog tick because
        // the TCB pointer is no longer valid.
        #if !APP_SIMULATION_MODE
    const esp_err_t wdtDelErr = esp_task_wdt_delete(nullptr);
    if (wdtDelErr != ESP_OK) {
        LOG_WARN("WiFi", "WDT delete(wifi_ap) failed: %d", static_cast<int>(wdtDelErr));
    }
        #endif

    s_active = false;
    s_taskHandle = nullptr;
    LOG_INFO("WiFi", "AP stopped");
    vTaskDelete(nullptr);
}

} // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void WifiAp::start() {
    if (s_active)
        return;
    buildSsid();      // build SSID before task starts so getSsid() is valid immediately
    ensurePassword(); // ditto for getPassword(); persists to NVS on first boot
    s_active = true;
    // Core 1, priority 5, stack 4096 B (see TASK_CORE_WIFI/PRIO_WIFI/STACK_WIFI in app_config.h).
    // On-demand task started only for OTA — runs co-resident with UI on core 1
    // because WiFi softAP needs the same Arduino-WiFi stack as the AP HTTP server.
    xTaskCreatePinnedToCore(apTaskFn, "wifi_ap", TASK_STACK_WIFI, nullptr, TASK_PRIO_WIFI,
                            &s_taskHandle, TASK_CORE_WIFI);
}

void WifiAp::stop() {
    s_active = false;
}

bool WifiAp::isActive() {
    return s_active;
}

const char *WifiAp::getSsid() {
    return s_ssid;
}

const char *WifiAp::getPassword() {
    ensurePassword(); // safe lazy fallback if start() hasn't run yet
    return s_password;
}

    #else // !APP_WIFI_OTA_ENABLED — stubs

void WifiAp::start() {
    LOG_WARN("WiFi", "WiFi OTA disabled at compile time (APP_WIFI_OTA_ENABLED=0)");
}
void WifiAp::stop() {}
bool WifiAp::isActive() {
    return false;
}
const char *WifiAp::getSsid() {
    return "";
}
const char *WifiAp::getPassword() {
    return "";
}

    #endif // APP_WIFI_OTA_ENABLED

#endif // APP_BLE_ENABLED
