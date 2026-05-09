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

#include <WiFi.h>
#include <WebServer.h>
#include <Update.h>
#include <Arduino.h>
#include <Preferences.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <stdio.h>
#include <string.h>

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

static WebServer s_server(80);
static TaskHandle_t s_taskHandle = nullptr;
static volatile bool s_active = false;
static char s_ssid[20] = {};

// Per-device AP password — 16 hex chars + null terminator. Generated on first
// boot via esp_random() (64 bits entropy) and persisted in NVS.
static char s_password[17] = {};

static constexpr char NVS_NS_WIFI_AP[] = "wifi_ap";
static constexpr char NVS_KEY_PWD[] = "pwd";
static constexpr size_t AP_PASSWORD_LEN = 16;

// ---------------------------------------------------------------------------
// HTTP handlers
// ---------------------------------------------------------------------------

namespace {

void handleStatus() {
    char buf[64];
    snprintf(buf, sizeof(buf), "{\"status\":\"ok\",\"ver\":\"%s\"}", APP_VERSION_STR);
    s_server.send(200, "application/json", buf);
}

void handleOtaComplete() {
    bool ok = !Update.hasError();
    const char *body = ok ? "{\"status\":\"ok\"}" : "{\"status\":\"error\"}";
    s_server.send(ok ? 200 : 500, "application/json", body);
    if (ok) {
        LOG_INFO("WiFi", "OTA complete — rebooting");
        delay(200);
        esp_restart();
    } else {
        LOG_ERROR("WiFi", "OTA failed: %s", Update.errorString());
    }
}

void handleOtaUpload() {
    HTTPUpload &upload = s_server.upload();
    if (upload.status == UPLOAD_FILE_START) {
        LOG_INFO("WiFi", "OTA upload start: %s (%u bytes expected)",
                 upload.filename.c_str(), upload.totalSize);
        if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
            LOG_ERROR("WiFi", "Update.begin failed: %s", Update.errorString());
        }
    } else if (upload.status == UPLOAD_FILE_WRITE) {
        if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
            LOG_ERROR("WiFi", "Update.write mismatch");
        }
    } else if (upload.status == UPLOAD_FILE_END) {
        if (Update.end(true)) {
            LOG_INFO("WiFi", "OTA upload done: %u bytes written", upload.totalSize);
        } else {
            LOG_ERROR("WiFi", "Update.end failed: %s", Update.errorString());
        }
    }
}

void buildSsid() {
    uint8_t mac[6];
    WiFi.macAddress(mac);
    snprintf(s_ssid, sizeof(s_ssid), "CANShift-%02X%02X", mac[4], mac[5]);
}

void ensurePassword() {
    if (s_password[0] != '\0') return; // already loaded this boot

    Preferences p;
    if (p.begin(NVS_NS_WIFI_AP, /*readOnly=*/true)) {
        const size_t len = p.getString(NVS_KEY_PWD, s_password, sizeof(s_password));
        p.end();
        if (len == AP_PASSWORD_LEN) return; // valid persisted value
    }

    // Missing, empty, or wrong length — generate a fresh 64-bit random password.
    const uint32_t a = esp_random();
    const uint32_t b = esp_random();
    snprintf(s_password, sizeof(s_password), "%08x%08x", a, b);

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
    WiFi.softAP(s_ssid, s_password);
    LOG_INFO("WiFi", "AP started — SSID: %s  IP: %s", s_ssid,
             WiFi.softAPIP().toString().c_str());

    s_server.on("/status", HTTP_GET, handleStatus);
    s_server.on("/ota", HTTP_POST, handleOtaComplete, handleOtaUpload);
    s_server.begin();

    const uint32_t startMs = millis();
    while (s_active && (millis() - startMs < BLE_WIFI_AP_TIMEOUT_MS)) {
        s_server.handleClient();
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    s_server.stop();
    WiFi.softAPdisconnect(true);
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
    if (s_active) return;
    buildSsid();      // build SSID before task starts so getSsid() is valid immediately
    ensurePassword(); // ditto for getPassword(); persists to NVS on first boot
    s_active = true;
    xTaskCreatePinnedToCore(apTaskFn, "wifi_ap", TASK_STACK_WIFI, nullptr,
                            TASK_PRIO_WIFI, &s_taskHandle, TASK_CORE_WIFI);
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
