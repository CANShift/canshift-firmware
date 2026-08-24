#include "app_config.h"
#if APP_BLE_ENABLED

    #include "ble_server.h"
    #include "ble_server_internal.h"

    #include "config/json_reader.h"
    #include "config/rotation_config.h"
    #include "diag/error_store.h"
    #include "diag/logger.h"
    #include "diag/lvgl_hold_timer.h"
    #include "hal/storage/nvs_store.h"
    #include "runtime/lvgl_lock.h"
    #include "runtime/device_commands.h"
    #include "runtime/pending_actions.h"
    #include "runtime/timer_service.h"
    #include "ui/settings_page.h"

    #include <ArduinoJson.h>
    #include <Arduino.h>
    #include <NimBLEDevice.h>
    #include <atomic>
    #include <esp_heap_caps.h>
    #include <esp_random.h>
    #include <freertos/semphr.h>
    #include <string.h>

static constexpr char SVC_UUID[] = "4fa0b6a0-0000-0000-0000-000000000001";
static constexpr char TELE_UUID[] = "4fa0b6a0-0000-0000-0000-000000000002";
static constexpr char STATUS_UUID[] = "4fa0b6a0-0000-0000-0000-000000000003";
static constexpr char SETTINGS_UUID[] = "4fa0b6a0-0000-0000-0000-000000000004";
static constexpr char CMD_UUID[] = "4fa0b6a0-0000-0000-0000-000000000005";
static constexpr char TIMER_CMD_UUID[] = "4fa0b6a0-0000-0000-0000-000000000006";
static constexpr char TIMER_STATE_UUID[] = "4fa0b6a0-0000-0000-0000-000000000007";
static constexpr char TIMER_LAP_UUID[] = "4fa0b6a0-0000-0000-0000-000000000008";

static constexpr size_t BLE_MIN_HEAP = BLE_MIN_HEAP_BYTES;

static constexpr size_t BLE_MAX_WRITE_LEN = 256U;
static constexpr uint32_t BLE_SETTINGS_MUTEX_TIMEOUT_MS = 50U;

static constexpr uint16_t BLE_PREFERRED_MTU = 247U;

static bool s_stackInited = false;
static bool s_gattInited = false;

namespace BleServerInternal {
NimBLECharacteristic *s_pTele = nullptr;
NimBLECharacteristic *s_pStatus = nullptr;
NimBLECharacteristic *s_pTimerState = nullptr;
NimBLECharacteristic *s_pTimerLap = nullptr;
bool s_connected = false;
} // namespace BleServerInternal

static bool s_enabled = false;

static std::atomic<bool> s_stopRequested{false};

namespace {

constexpr uint16_t CONN_MIN_INTERVAL = 12;
constexpr uint16_t CONN_MAX_INTERVAL = 24;
constexpr uint16_t CONN_SLAVE_LATENCY = 4;
constexpr uint16_t CONN_SUPERVISION_TIMEOUT = 400;

class ServerCallbacks : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer *pServer, ble_gap_conn_desc *desc) override {
        BleServerInternal::s_connected = true;
        pServer->updateConnParams(desc->conn_handle, CONN_MIN_INTERVAL, CONN_MAX_INTERVAL,
                                  CONN_SLAVE_LATENCY, CONN_SUPERVISION_TIMEOUT);
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

        JsonVariantConst brightnessVar = doc["brightness"];
        if (!brightnessVar.isNull()) {
            const uint8_t brightness = brightnessVar.as<uint8_t>();
            LvglLock lock(pdMS_TO_TICKS(BLE_SETTINGS_MUTEX_TIMEOUT_MS));
            if (!lock.held()) {
                LOG_WARN("BLE", "SETTINGS write: LVGL mutex busy — brightness dropped");
                pChar->setValue("{\"err\":\"busy\"}");
                return;
            }
            LVGL_HOLD_TIMER(::PerfCounters::MUTEX_HOLD_BLE);
            SettingsPage::applyFromUsb(brightness);
            LOG_DEBUG("BLE", "Settings applied via BLE");
        }

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
        const DeviceCommands::Command *entry = DeviceCommands::findByBleName(cmd);
        if (entry == nullptr) {
            LOG_WARN("BLE", "Unknown CMD: %s", cmd);
            return;
        }

        if (entry->run(doc.as<JsonObjectConst>()) == DeviceCommands::Outcome::RebootRequested) {
            delay(PRE_RESTART_FLUSH_DELAY_MS);
            esp_restart();
        }
    }
};

class TimerCmdCallbacks : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic *pChar) override {
        std::string val = pChar->getValue();
        if (val.empty())
            return;
        if (val.length() > BLE_MAX_WRITE_LEN) {
            LOG_WARN("BLE", "TIMER cmd write %u bytes exceeds cap %u — dropping",
                     static_cast<unsigned>(val.length()), static_cast<unsigned>(BLE_MAX_WRITE_LEN));
            return;
        }

        JsonDocument doc;
        if (JsonReader::parse(doc, val.c_str(), val.length()) != DeserializationError::Ok)
            return;

        JsonVariantConst opVar = doc["op"];
        if (opVar.isNull() || !opVar.is<int>()) {
            LOG_WARN("BLE", "TIMER cmd missing 'op' int — ignoring");
            return;
        }

        const int op = opVar.as<int>();
        bool accepted = false;
        switch (op) {
            case 1:
                accepted = TimerService::start();
                break;
            case 2:
                accepted = TimerService::pause();
                break;
            case 3:
                accepted = TimerService::resume();
                break;
            case 4:
                accepted = TimerService::reset();
                break;
            case 5:
                accepted = TimerService::lap();
                break;
            default:
                LOG_WARN("BLE", "Unknown TIMER op: %d", op);
                return;
        }
        LOG_DEBUG("BLE", "TIMER op %d %s", op, accepted ? "applied" : "rejected");
    }
};

class TimerStateCallbacks : public NimBLECharacteristicCallbacks {
    void onRead(NimBLECharacteristic *) override {
        BleServerInternal::refreshTimerStateValue();
    }
};

NimBLECharacteristic *requireChar(NimBLEService *svc, const char *uuid, uint32_t props) {
    NimBLECharacteristic *ch = svc->createCharacteristic(uuid, props);
    if (ch == nullptr) {
        LOG_ERROR("BLE", "GATT characteristic alloc failed (%s)", uuid);
    }
    return ch;
}

bool createGattCharacteristics(NimBLEService *pSvc) {
    static SettingsCallbacks s_settingsCb;
    static CmdCallbacks s_cmdCb;
    static TimerCmdCallbacks s_timerCmdCb;
    static TimerStateCallbacks s_timerStateCb;

    BleServerInternal::s_pTele =
        requireChar(pSvc, TELE_UUID, NIMBLE_PROPERTY::READ_ENC | NIMBLE_PROPERTY::NOTIFY);
    BleServerInternal::s_pStatus =
        requireChar(pSvc, STATUS_UUID, NIMBLE_PROPERTY::READ_ENC | NIMBLE_PROPERTY::NOTIFY);
    NimBLECharacteristic *pSettings =
        requireChar(pSvc, SETTINGS_UUID, NIMBLE_PROPERTY::READ_ENC | NIMBLE_PROPERTY::WRITE_ENC);
    NimBLECharacteristic *pCmd =
        requireChar(pSvc, CMD_UUID, NIMBLE_PROPERTY::WRITE_ENC | NIMBLE_PROPERTY::WRITE_NR);
    NimBLECharacteristic *pTimerCmd =
        requireChar(pSvc, TIMER_CMD_UUID, NIMBLE_PROPERTY::WRITE_ENC | NIMBLE_PROPERTY::WRITE_NR);
    BleServerInternal::s_pTimerState =
        requireChar(pSvc, TIMER_STATE_UUID, NIMBLE_PROPERTY::READ_ENC | NIMBLE_PROPERTY::NOTIFY);
    BleServerInternal::s_pTimerLap = requireChar(pSvc, TIMER_LAP_UUID, NIMBLE_PROPERTY::NOTIFY);

    if (!BleServerInternal::s_pTele || !BleServerInternal::s_pStatus || !pSettings || !pCmd ||
        !pTimerCmd || !BleServerInternal::s_pTimerState || !BleServerInternal::s_pTimerLap) {
        return false;
    }

    BleServerInternal::s_pTele->setValue("{}");
    BleServerInternal::updateStatus();
    pSettings->setCallbacks(&s_settingsCb);
    pCmd->setCallbacks(&s_cmdCb);
    pTimerCmd->setCallbacks(&s_timerCmdCb);
    BleServerInternal::s_pTimerState->setCallbacks(&s_timerStateCb);
    BleServerInternal::s_pTimerState->setValue("{\"st\":0,\"el\":0,\"lc\":0,\"sid\":0,\"ver\":0}");
    return true;
}

bool setupGatt() {
    static ServerCallbacks s_serverCb;

    NimBLEServer *pServer = NimBLEDevice::createServer();
    if (pServer == nullptr) {
        LOG_ERROR("BLE", "GATT server alloc failed");
        return false;
    }
    pServer->setCallbacks(&s_serverCb);

    NimBLEService *pSvc = pServer->createService(SVC_UUID);
    if (pSvc == nullptr) {
        LOG_ERROR("BLE", "GATT service alloc failed");
        return false;
    }
    if (!createGattCharacteristics(pSvc)) {
        return false;
    }
    if (!pSvc->start()) {
        LOG_ERROR("BLE", "GATT service start failed");
        return false;
    }

    NimBLEAdvertising *pAdv = NimBLEDevice::getAdvertising();
    pAdv->addServiceUUID(SVC_UUID);
    pAdv->setName("CANShift");
    pAdv->setScanResponse(false);
    if (!pAdv->start()) {
        LOG_ERROR("BLE", "advertising start failed");
        return false;
    }

    s_gattInited = true;
    s_enabled = true;
    LOG_INFO("BLE", "Advertising as 'CANShift' — %s",
             NimBLEDevice::getAddress().toString().c_str());
    return true;
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
        NimBLEDevice::setMTU(BLE_PREFERRED_MTU);

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

    return setupGatt();
}

void reportStartFailure() {
    LOG_ERROR("BLE", "BLE stack start failed");
    ErrorStore::push(ERROR_SRC_SYSTEM, "ble_start", "BLE stack start failed");
    BleServer::requestStop();
}

} // namespace

void BleServer::earlyInit() {
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

    if (!setupGatt()) {
        ErrorStore::push(ERROR_SRC_SYSTEM, "ble_start", "BLE GATT setup failed");
    }
}

void BleServer::init() {
    if (!startStack()) {
        reportStartFailure();
        return;
    }
    BleServerInternal::updateStatus();
}

void BleServer::start() {
    if (s_enabled)
        return;
    if (!startStack()) {
        reportStartFailure();
    }
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
        BleServerInternal::s_pTimerState = nullptr;
        BleServerInternal::s_pTimerLap = nullptr;
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

void BleServer::requestStop() {
    s_stopRequested.store(true, std::memory_order_relaxed);
}

bool BleServer::takeStopRequest() {
    return s_stopRequested.exchange(false, std::memory_order_relaxed);
}

void BleServer::tick() {
    if (!BleServerInternal::s_connected)
        return;
    BleServerInternal::emitTelemetry();
    BleServerInternal::emitTimerSync();
}

bool BleServer::isConnected() {
    return BleServerInternal::s_connected;
}

#endif
