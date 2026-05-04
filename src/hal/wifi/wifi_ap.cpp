// wifi_ap.cpp — WiFi AP mode + HTTP OTA server

#include "wifi_ap.h"
#include "diag/logger.h"
#include "app_config.h"

#include <WiFi.h>
#include <WebServer.h>
#include <Update.h>
#include <Arduino.h>
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

void apTaskFn(void *) {
    buildSsid();
    WiFi.softAP(s_ssid, BLE_WIFI_AP_PASSWORD);
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
