// ble_server.cpp — BLE GATT server (NimBLE stack)

#include "app_config.h"
#if APP_BLE_ENABLED

#include "ble_server.h"
#include "hal/wifi/wifi_ap.h"
#include "ui/theme_manager.h"
#include "runtime/signal_store.h"
#include "can/signal_map.h"
#include "ui/settings_page.h"
#include "diag/logger.h"
#include "app_config.h"

#include <NimBLEDevice.h>
#include <ArduinoJson.h>
#include <freertos/semphr.h>
#include <Arduino.h>
#include <atomic>
#include <string.h>

// ---------------------------------------------------------------------------
// LVGL mutex (defined in main.cpp)
// ---------------------------------------------------------------------------

extern SemaphoreHandle_t g_lvglMutex;

// ---------------------------------------------------------------------------
// UUIDs
// ---------------------------------------------------------------------------

static constexpr char SVC_UUID[]      = "4fa0b6a0-0000-0000-0000-000000000001";
static constexpr char TELE_UUID[]     = "4fa0b6a0-0000-0000-0000-000000000002";
static constexpr char STATUS_UUID[]   = "4fa0b6a0-0000-0000-0000-000000000003";
static constexpr char SETTINGS_UUID[] = "4fa0b6a0-0000-0000-0000-000000000004";
static constexpr char CMD_UUID[]      = "4fa0b6a0-0000-0000-0000-000000000005";

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

static NimBLECharacteristic *s_pTele     = nullptr;
static NimBLECharacteristic *s_pStatus   = nullptr;
static bool s_connected = false;

// Deferred command flags — set by BLE callbacks, consumed by UI task
static std::atomic<bool> s_pendingDayNightToggle{false};
static std::atomic<bool> s_pendingCalibration{false};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace {

void addSignalIfValid(JsonDocument &doc, const char *key, SignalId id) {
    if (SignalStore::isValid(id))
        doc[key] = roundf(SignalStore::read(id) * 10.0f) / 10.0f; // 1 decimal place
}

void updateStatus() {
    if (!s_pStatus) return;
    JsonDocument doc;
    doc["ver"]    = APP_VERSION_STR;
    doc["can"]    = SignalStore::isValid(SignalIds::RPM) ? 1 : 0;
    doc["is_day"] = ThemeManager::isDayMode() ? 1 : 0;
    if (WifiAp::isActive()) doc["ap_ssid"] = WifiAp::getSsid();
    char buf[128];
    serializeJson(doc, buf, sizeof(buf));
    s_pStatus->setValue(buf);
}

// ---------------------------------------------------------------------------
// Server callbacks — connection lifecycle
// ---------------------------------------------------------------------------

class ServerCallbacks : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer *pServer, ble_gap_conn_desc *desc) override {
        s_connected = true;
        // Request faster connection interval for smoother telemetry
        pServer->updateConnParams(desc->conn_handle, 12, 24, 0, 400);
        LOG_INFO("BLE", "Client connected: %s",
                 NimBLEAddress(desc->peer_ota_addr).toString().c_str());
    }
    void onDisconnect(NimBLEServer *pServer) override {
        s_connected = false;
        LOG_INFO("BLE", "Client disconnected — restarting advertising");
        NimBLEDevice::startAdvertising();
    }
};

// ---------------------------------------------------------------------------
// SETTINGS write callback
// Payload: {"brightness":80,"sleep":30,"rotation":0}
// ---------------------------------------------------------------------------

class SettingsCallbacks : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic *pChar) override {
        std::string val = pChar->getValue();
        if (val.empty()) return;

        JsonDocument doc;
        if (deserializeJson(doc, val.c_str(), val.length()) != DeserializationError::Ok)
            return;

        uint8_t brightness  = doc["brightness"] | 80;
        uint32_t sleepS     = doc["sleep"] | 0u;
        uint16_t rotation   = doc["rotation"] | 0u;

        if (xSemaphoreTake(g_lvglMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            SettingsPage::applyFromUsb(brightness, sleepS, rotation);
            xSemaphoreGive(g_lvglMutex);
        }
        LOG_DEBUG("BLE", "Settings applied via BLE");
    }
};

// ---------------------------------------------------------------------------
// CMD write callback
// Payload: {"cmd":"start_wifi_ap"} | {"cmd":"reboot"}
// ---------------------------------------------------------------------------

class CmdCallbacks : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic *pChar) override {
        std::string val = pChar->getValue();
        if (val.empty()) return;

        JsonDocument doc;
        if (deserializeJson(doc, val.c_str(), val.length()) != DeserializationError::Ok)
            return;

        const char *cmd = doc["cmd"] | "";

        if (strcmp(cmd, "start_wifi_ap") == 0) {
            WifiAp::start();
            LOG_INFO("BLE", "CMD: starting WiFi AP — SSID: %s", WifiAp::getSsid());
            updateStatus();
            if (s_pStatus->getSubscribedCount() > 0) s_pStatus->notify();
        } else if (strcmp(cmd, "stop_wifi_ap") == 0) {
            LOG_INFO("BLE", "CMD: stopping WiFi AP");
            WifiAp::stop();
            updateStatus();
            if (s_pStatus->getSubscribedCount() > 0) s_pStatus->notify();
        } else if (strcmp(cmd, "toggle_day_night") == 0) {
            // Deferred to UI task — ThemeManager requires LVGL mutex from UI context
            s_pendingDayNightToggle.store(true, std::memory_order_relaxed);
            LOG_INFO("BLE", "CMD: day/night toggle queued");
        } else if (strcmp(cmd, "start_calibration") == 0) {
            // Deferred to UI task — calibrate() is blocking (user taps crosshairs)
            s_pendingCalibration.store(true, std::memory_order_relaxed);
            LOG_INFO("BLE", "CMD: calibration queued");
        } else if (strcmp(cmd, "reboot") == 0) {
            LOG_INFO("BLE", "CMD: reboot");
            delay(100);
            esp_restart();
        } else {
            LOG_WARN("BLE", "Unknown CMD: %s", cmd);
        }
    }
};

} // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void BleServer::init() {
    NimBLEDevice::init("CANShift");
    NimBLEDevice::setPower(ESP_PWR_LVL_P9);

    NimBLEServer *pServer = NimBLEDevice::createServer();
    pServer->setCallbacks(new ServerCallbacks());

    NimBLEService *pSvc = pServer->createService(SVC_UUID);

    // TELE — notify, live signal stream
    s_pTele = pSvc->createCharacteristic(TELE_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
    s_pTele->setValue("{}");

    // STATUS — read + notify: firmware version, CAN health, WiFi AP SSID when active
    s_pStatus = pSvc->createCharacteristic(
        STATUS_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
    updateStatus();

    // SETTINGS — write with response, screen settings
    NimBLECharacteristic *pSettings = pSvc->createCharacteristic(
        SETTINGS_UUID, NIMBLE_PROPERTY::WRITE);
    pSettings->setCallbacks(new SettingsCallbacks());

    // CMD — write without response, device commands
    NimBLECharacteristic *pCmd = pSvc->createCharacteristic(
        CMD_UUID, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
    pCmd->setCallbacks(new CmdCallbacks());

    pSvc->start();

    NimBLEAdvertising *pAdv = NimBLEDevice::getAdvertising();
    pAdv->addServiceUUID(SVC_UUID);
    pAdv->setName("CANShift");
    pAdv->setScanResponse(false);
    pAdv->start();

    LOG_INFO("BLE", "Advertising as 'CANShift' — %s", NimBLEDevice::getAddress().toString().c_str());
}

void BleServer::tick() {
    if (!s_connected || !s_pTele) return;
    if (!s_pTele->getSubscribedCount()) return;

    JsonDocument doc;
    addSignalIfValid(doc, "r",   SignalIds::RPM);
    addSignalIfValid(doc, "tps", SignalIds::THROTTLE_POS);
    addSignalIfValid(doc, "map", SignalIds::MAP_KPA);
    addSignalIfValid(doc, "bst", SignalIds::BOOST_BAR);
    addSignalIfValid(doc, "iat", SignalIds::IAT_C);
    addSignalIfValid(doc, "ct",  SignalIds::COOLANT_TEMP_C);
    addSignalIfValid(doc, "ot",  SignalIds::OIL_TEMP_C);
    addSignalIfValid(doc, "op",  SignalIds::OIL_PRESS_BAR);
    addSignalIfValid(doc, "fp",  SignalIds::FUEL_PRESS_BAR);
    addSignalIfValid(doc, "lam", SignalIds::LAMBDA_1);
    addSignalIfValid(doc, "s",   SignalIds::SPEED_KPH);
    addSignalIfValid(doc, "g",   SignalIds::GEAR);
    addSignalIfValid(doc, "bat", SignalIds::BATTERY_VOLTS);

    char buf[512];
    size_t len = serializeJson(doc, buf, sizeof(buf));
    s_pTele->setValue(reinterpret_cast<uint8_t *>(buf), len);
    s_pTele->notify();

    // Refresh STATUS every 2s; notify if AP state changed (e.g. timeout)
    static uint8_t s_statusDiv = 0;
    static bool s_prevApActive = false;
    if (++s_statusDiv >= 20) {
        updateStatus();
        s_statusDiv = 0;
        bool apNow = WifiAp::isActive();
        if (apNow != s_prevApActive) {
            s_prevApActive = apNow;
            if (s_pStatus->getSubscribedCount() > 0) s_pStatus->notify();
        }
    }
}

bool BleServer::isConnected() {
    return s_connected;
}

void BleServer::pushStatusNotify() {
    updateStatus();
    if (s_pStatus && s_pStatus->getSubscribedCount() > 0) s_pStatus->notify();
}

bool BleServer::takePendingDayNightToggle() {
    return s_pendingDayNightToggle.exchange(false, std::memory_order_relaxed);
}

bool BleServer::takePendingCalibration() {
    return s_pendingCalibration.exchange(false, std::memory_order_relaxed);
}

#endif // APP_BLE_ENABLED
