#include "app_config.h"
#if APP_BLE_ENABLED

    #include "ble_server.h"
    #include "ble_server_internal.h"

    #include "config/json_reader.h"
    #include "config/rotation_config.h"
    #include "diag/logger.h"
    #include "diag/lvgl_lock_guard.h"
    #include "runtime/pending_actions.h"
    #include "runtime/track_store.h"
    #include "ui/settings_page.h"

    #include <ArduinoJson.h>
    #include <Arduino.h>
    #include <NimBLEDevice.h>
    #include <Preferences.h>
    #include <atomic>
    #include <esp_heap_caps.h>
    #include <esp_random.h>
    #include <freertos/semphr.h>
    #include <string.h>

extern SemaphoreHandle_t g_lvglMutex;

static constexpr char SVC_UUID[] = "4fa0b6a0-0000-0000-0000-000000000001";
static constexpr char TELE_UUID[] = "4fa0b6a0-0000-0000-0000-000000000002";
static constexpr char STATUS_UUID[] = "4fa0b6a0-0000-0000-0000-000000000003";
static constexpr char SETTINGS_UUID[] = "4fa0b6a0-0000-0000-0000-000000000004";
static constexpr char CMD_UUID[] = "4fa0b6a0-0000-0000-0000-000000000005";

static constexpr size_t BLE_MIN_HEAP = BLE_MIN_HEAP_BYTES;

static constexpr size_t BLE_MAX_WRITE_LEN = 256U;

static bool s_stackInited = false;
static bool s_gattInited = false;

namespace BleServerInternal {
NimBLECharacteristic *s_pTele = nullptr;
NimBLECharacteristic *s_pStatus = nullptr;
bool s_connected = false;
} // namespace BleServerInternal

static bool s_enabled = false;

static std::atomic<int8_t> s_pendingEnabled{-1};

namespace {

class ServerCallbacks : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer *pServer, ble_gap_conn_desc *desc) override {
        BleServerInternal::s_connected = true;
        pServer->updateConnParams(desc->conn_handle, 12, 24, 0, 400);
        LOG_INFO("BLE", "Client connected: %s",
                 NimBLEAddress(desc->peer_ota_addr).toString().c_str());
    }
    void onDisconnect(NimBLEServer *pServer) override {
        BleServerInternal::s_connected = false;
        LOG_INFO("BLE", "Client disconnected — restarting advertising");
        PendingActions::blePasskeyHide.store(true, std::memory_order_relaxed);
        NimBLEDevice::startAdvertising();
    }

    uint32_t onPassKeyRequest() override {
        constexpr uint32_t PASSKEY_RANGE = 1000000u;
        constexpr uint32_t REJECTION_LIMIT = UINT32_MAX - (UINT32_MAX % PASSKEY_RANGE);
        uint32_t passkey;
        do {
            uint32_t draw = esp_random();
            while (draw >= REJECTION_LIMIT) {
                draw = esp_random();
            }
            passkey = draw % PASSKEY_RANGE;
        } while (passkey == 0u);
        LOG_INFO("BLE", "Pairing requested — passkey %06u", static_cast<unsigned>(passkey));
        PendingActions::blePasskeyShow.store(passkey, std::memory_order_relaxed);
        return passkey;
    }

    void onAuthenticationComplete(ble_gap_conn_desc *desc) override {
        const bool encrypted = (desc != nullptr) && desc->sec_state.encrypted;
        if (encrypted) {
            LOG_INFO("BLE", "Pairing complete — link encrypted");
        } else {
            LOG_WARN("BLE", "Pairing did not produce an encrypted link");
        }
        PendingActions::blePasskeyHide.store(true, std::memory_order_relaxed);
    }
};

class SettingsCallbacks : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic *pChar) override {
        std::string val = pChar->getValue();
        if (val.empty())
            return;
        if (val.length() > BLE_MAX_WRITE_LEN) {
            LOG_WARN("BLE", "SETTINGS write %u bytes exceeds cap %u — dropping",
                     static_cast<unsigned>(val.length()), static_cast<unsigned>(BLE_MAX_WRITE_LEN));
            pChar->setValue("{\"err\":\"too_long\"}");
            return;
        }

        JsonDocument doc;
        if (JsonReader::parse(doc, val.c_str(), val.length()) != DeserializationError::Ok)
            return;

        uint8_t brightness = doc["brightness"] | 80;

        if (xSemaphoreTake(g_lvglMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            LVGL_HOLD_GUARD(::PerfCounters::MUTEX_HOLD_BLE);
            SettingsPage::applyFromUsb(brightness);
            xSemaphoreGive(g_lvglMutex);
        }
        LOG_DEBUG("BLE", "Settings applied via BLE");

        JsonVariantConst rotationVar = doc["rotation"];
        if (!rotationVar.isNull()) {
            const uint16_t rotation = rotationVar.as<uint16_t>();
            if ((rotation == 0 || rotation == 180) && rotation != RotationConfig::getOffsetDeg()) {
                LOG_INFO("BLE", "Rotation change requested: %u° — rebooting", rotation);
                RotationConfig::applyAndReboot(rotation);
            }
        }
    }

    void onRead(NimBLECharacteristic *pChar) override {
        JsonDocument doc;
        doc["brightness"] = SettingsPage::getBrightness();
        char buf[64];

        const size_t len = serializeJson(doc, buf, sizeof(buf));
        if (len == 0 || len >= sizeof(buf)) {
            LOG_WARN("BLE", "SETTINGS read payload truncated (len=%u, cap=%u) — emitting canary",
                     static_cast<unsigned>(len), static_cast<unsigned>(sizeof(buf)));
            pChar->setValue("{\"err\":\"too_long\"}");
            return;
        }
        pChar->setValue(buf);
    }
};

class CmdCallbacks : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic *pChar) override {
        std::string val = pChar->getValue();
        if (val.empty())
            return;
        if (val.length() > BLE_MAX_WRITE_LEN) {
            LOG_WARN("BLE", "CMD write %u bytes exceeds cap %u — dropping",
                     static_cast<unsigned>(val.length()), static_cast<unsigned>(BLE_MAX_WRITE_LEN));
            return;
        }

        JsonDocument doc;
        if (JsonReader::parse(doc, val.c_str(), val.length()) != DeserializationError::Ok)
            return;

        const char *cmd = doc["cmd"] | "";

        if (strcmp(cmd, "toggle_day_night") == 0) {
            PendingActions::dayNightToggle.store(true, std::memory_order_relaxed);
            LOG_INFO("BLE", "CMD: day/night toggle queued");
        } else if (strcmp(cmd, "set_day_night") == 0) {
            JsonVariantConst dayVar = doc["day"];
            if (dayVar.isNull() || !dayVar.is<bool>()) {
                LOG_WARN("BLE", "set_day_night missing 'day' bool — ignoring");
            } else {
                const bool day = dayVar.as<bool>();
                PendingActions::dayNightSet.store(day ? 1 : 0, std::memory_order_relaxed);
                LOG_INFO("BLE", "CMD: day/night set queued — %s", day ? "day" : "night");
            }
        } else if (strcmp(cmd, "start_calibration") == 0) {
            PendingActions::touchCalibrate.store(true, std::memory_order_relaxed);
            LOG_INFO("BLE", "CMD: calibration queued");
        } else if (strcmp(cmd, "reset_calibration") == 0) {
            PendingActions::touchCalibrationReset.store(true, std::memory_order_relaxed);
            LOG_INFO("BLE", "CMD: calibration reset queued");
        } else if (strcmp(cmd, "track_state") == 0) {

            TrackStore::State next = {};
            next.trackMode = doc["trackMode"] | false;
            next.currentLapMs = doc["currentLapMs"] | 0;
            next.lastLapMs = doc["lastLapMs"] | 0;
            next.bestLapMs = doc["bestLapMs"] | 0;
            const int lapNum = doc["lapNumber"] | 0;
            next.lapNumber =
                (lapNum < 0) ? 0 : static_cast<uint16_t>(lapNum > 9999 ? 9999 : lapNum);
            next.deltaMs = doc["deltaMs"] | 0;
            next.isBestLap = doc["isBestLap"] | false;
            TrackStore::setTelemetry(next);
        } else if (strcmp(cmd, "reboot") == 0) {
            LOG_INFO("BLE", "CMD: reboot");
            delay(PRE_RESTART_FLUSH_DELAY_MS);
            esp_restart();
        } else {
            LOG_WARN("BLE", "Unknown CMD: %s", cmd);
        }
    }
};

void setupGatt() {

    static ServerCallbacks s_serverCb;
    static SettingsCallbacks s_settingsCb;
    static CmdCallbacks s_cmdCb;

    NimBLEServer *pServer = NimBLEDevice::createServer();
    pServer->setCallbacks(&s_serverCb);

    NimBLEService *pSvc = pServer->createService(SVC_UUID);

    BleServerInternal::s_pTele =
        pSvc->createCharacteristic(TELE_UUID, NIMBLE_PROPERTY::READ_ENC | NIMBLE_PROPERTY::NOTIFY);
    BleServerInternal::s_pTele->setValue("{}");

    BleServerInternal::s_pStatus = pSvc->createCharacteristic(
        STATUS_UUID, NIMBLE_PROPERTY::READ_ENC | NIMBLE_PROPERTY::NOTIFY);

    BleServerInternal::s_pStatus->setValue("{\"ver\":\"" APP_VERSION_STR
                                           "\",\"can\":0,\"is_day\":0}");

    NimBLECharacteristic *pSettings = pSvc->createCharacteristic(
        SETTINGS_UUID, NIMBLE_PROPERTY::READ_ENC | NIMBLE_PROPERTY::WRITE_ENC);
    pSettings->setCallbacks(&s_settingsCb);

    NimBLECharacteristic *pCmd = pSvc->createCharacteristic(
        CMD_UUID, NIMBLE_PROPERTY::WRITE_ENC | NIMBLE_PROPERTY::WRITE_NR);
    pCmd->setCallbacks(&s_cmdCb);

    pSvc->start();

    NimBLEAdvertising *pAdv = NimBLEDevice::getAdvertising();
    pAdv->addServiceUUID(SVC_UUID);
    pAdv->setName("CANShift");
    pAdv->setScanResponse(false);
    pAdv->start();

    s_gattInited = true;
    s_enabled = true;
    LOG_INFO("BLE", "Advertising as 'CANShift' — %s",
             NimBLEDevice::getAddress().toString().c_str());
}

bool startStack() {
    if (s_gattInited) {

        NimBLEDevice::getAdvertising()->start();
        s_enabled = true;
        LOG_INFO("BLE", "BLE advertising restarted — %s",
                 NimBLEDevice::getAddress().toString().c_str());
        return true;
    }

    if (!s_stackInited) {

        const size_t avail = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
        if (avail < BLE_MIN_HEAP) {
            LOG_WARN("BLE", "Insufficient contiguous DRAM for BLE stack (%u B < %u B) — skipped",
                     static_cast<unsigned>(avail), static_cast<unsigned>(BLE_MIN_HEAP));
            return false;
        }
        NimBLEDevice::init("CANShift");
        NimBLEDevice::setPower(ESP_PWR_LVL_P9);

        NimBLEDevice::setSecurityAuth(true, true, true);
        NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_ONLY);
        s_stackInited = true;
    }

    const size_t heapFree = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    if (heapFree < BLE_GATT_MIN_HEAP_BYTES) {
        LOG_WARN("BLE", "Heap too low for GATT setup (%u B < %u B) — BLE disabled",
                 static_cast<unsigned>(heapFree), static_cast<unsigned>(BLE_GATT_MIN_HEAP_BYTES));
        return false;
    }

    setupGatt();
    return true;
}

} // namespace

void BleServer::earlyInit() {

    Preferences p;
    p.begin("screen_cfg", true);
    const bool enabled = p.getUChar("ble_en", BLE_DEFAULT_ENABLED) != 0;
    p.end();

    if (!enabled) {
        LOG_INFO("BLE", "BLE disabled in NVS — skipping early init");
        return;
    }

    const size_t avail = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    if (avail < BLE_MIN_HEAP) {
        LOG_WARN("BLE", "Insufficient contiguous DRAM for BLE early init (%u B < %u B)",
                 static_cast<unsigned>(avail), static_cast<unsigned>(BLE_MIN_HEAP));
        return;
    }

    NimBLEDevice::init("CANShift");
    NimBLEDevice::setPower(ESP_PWR_LVL_P9);

    NimBLEDevice::setSecurityAuth(true, true, true);
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_ONLY);
    s_stackInited = true;
    LOG_INFO("BLE", "BLE stack initialized (%u B available) — building GATT tree",
             static_cast<unsigned>(avail));

    setupGatt();
}

void BleServer::init() {
    if (!SettingsPage::getBleEnabled()) {
        LOG_INFO("BLE", "BLE disabled by user setting — skipping init");
        return;
    }
    startStack();
    if (s_enabled) {
        BleServerInternal::updateStatus();
    }
}

void BleServer::start() {
    if (s_enabled)
        return;
    startStack();
}

void BleServer::stop() {
    if (!s_enabled)
        return;
    if (s_gattInited) {

        NimBLEDevice::stopAdvertising();
        BleServerInternal::s_connected = false;
        s_enabled = false;
        LOG_INFO("BLE", "BLE advertising stopped (GATT preserved)");
    } else {

        BleServerInternal::s_pTele = nullptr;
        BleServerInternal::s_pStatus = nullptr;
        BleServerInternal::s_connected = false;

        std::atomic_thread_fence(std::memory_order_release);
        NimBLEDevice::deinit(true);
        s_enabled = false;
        s_stackInited = false;
        LOG_INFO("BLE", "BLE stack stopped — heap freed");
    }
}

bool BleServer::isEnabled() {
    return s_enabled;
}

void BleServer::setPendingEnabled(bool enabled) {
    s_pendingEnabled.store(enabled ? 1 : 0, std::memory_order_relaxed);
}

int8_t BleServer::takePendingEnabled() {
    return s_pendingEnabled.exchange(-1, std::memory_order_relaxed);
}

void BleServer::tick() {
    if (!BleServerInternal::s_connected)
        return;
    BleServerInternal::emitTelemetry();
}

bool BleServer::isConnected() {
    return BleServerInternal::s_connected;
}

#endif
