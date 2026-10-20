#include "app_config.h"
#if APP_BLE_ENABLED

    #include "ble_server.h"
    #include "ble_server_internal.h"
    #include "ble_link_rs.h"

    #include "diag/error_store.h"
    #include "diag/logger.h"
    #include "hal/storage/nvs_store.h"
    #include "runtime/pending_actions.h"

    #include <Arduino.h>
    #include <NimBLEDevice.h>
    #include <esp_heap_caps.h>
    #include <esp_random.h>
    #include <freertos/semphr.h>
    #include <string.h>

static constexpr char kSvcUuid[] = "4fa0b6a0-0000-0000-0000-000000000001";
static constexpr char kTeleUuid[] = "4fa0b6a0-0000-0000-0000-000000000002";
static constexpr char kStatusUuid[] = "4fa0b6a0-0000-0000-0000-000000000003";
static constexpr char kSettingsUuid[] = "4fa0b6a0-0000-0000-0000-000000000004";
static constexpr char kCmdUuid[] = "4fa0b6a0-0000-0000-0000-000000000005";
static constexpr char kTimerCmdUuid[] = "4fa0b6a0-0000-0000-0000-000000000006";
static constexpr char kTimerStateUuid[] = "4fa0b6a0-0000-0000-0000-000000000007";
static constexpr char kTimerLapUuid[] = "4fa0b6a0-0000-0000-0000-000000000008";
static constexpr char kAlertUuid[] = "4fa0b6a0-0000-0000-0000-000000000009";
static constexpr char kPagesUuid[] = "4fa0b6a0-0000-0000-0000-00000000000a";
static constexpr char kCanMonitorUuid[] = "4fa0b6a0-0000-0000-0000-00000000000b";

static constexpr size_t kBleMinHeap = kBleMinHeapBytes;

static constexpr uint16_t kBlePreferredMtu = 247U;

namespace BleServerInternal {
NimBLECharacteristic *s_pTele = nullptr;
NimBLECharacteristic *s_pStatus = nullptr;
NimBLECharacteristic *s_pTimerState = nullptr;
NimBLECharacteristic *s_pTimerLap = nullptr;
NimBLECharacteristic *s_pAlert = nullptr;
NimBLECharacteristic *s_pCanMonitor = nullptr;
bool s_connected = false;
bool s_stackInited = false;
bool s_gattInited = false;
bool s_enabled = false;
bool s_released = false;
}

using BleServerInternal::s_enabled;
using BleServerInternal::s_gattInited;
using BleServerInternal::s_released;
using BleServerInternal::s_stackInited;

namespace {

constexpr uint16_t kConnMinInterval = 12;
constexpr uint16_t kConnMaxInterval = 24;
constexpr uint16_t kConnSlaveLatency = 4;
constexpr uint16_t kConnSupervisionTimeout = 400;

uint32_t drawPasskey() {
    constexpr uint32_t kPasskeyRange = 1000000u;
    constexpr uint32_t kRejectionLimit = UINT32_MAX - (UINT32_MAX % kPasskeyRange);
    uint32_t passkey;
    do {
        uint32_t draw = esp_random();
        while (draw >= kRejectionLimit) {
            draw = esp_random();
        }
        passkey = draw % kPasskeyRange;
    } while (passkey == 0u);
    return passkey;
}

class ServerCallbacks : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer *pServer, ble_gap_conn_desc *desc) override {
        BleServerInternal::s_connected = true;
        BleServerInternal::noteConnected();
        pServer->updateConnParams(desc->conn_handle, kConnMinInterval, kConnMaxInterval,
                                  kConnSlaveLatency, kConnSupervisionTimeout);
        LOG_INFO("BLE", "Client connected: %s",
                 NimBLEAddress(desc->peer_ota_addr).toString().c_str());
    }
    void onDisconnect(NimBLEServer *pServer) override {
        BleServerInternal::s_connected = false;
        BleServerInternal::stopCanMonitor();
        LOG_INFO("BLE", "Client disconnected — restarting advertising");
        PendingActions::blePasskeyHide.store(true, std::memory_order_relaxed);
        if (!BleServerInternal::startAdvertisingFast())
            LOG_WARN("BLE", "Advertising not restarted after disconnect");
    }

    uint32_t onPassKeyRequest() override {
        const uint32_t passkey = drawPasskey();
        if (!BleServerInternal::admitPasskeyPrompt()) {
            BleServerInternal::dropCurrentPeer("Pairing attempts exceed the prompt budget");
            return passkey;
        }
        LOG_INFO("BLE", "Pairing requested — passkey %06u", static_cast<unsigned>(passkey));
        PendingActions::blePasskeyShow.store(passkey, std::memory_order_relaxed);
        return passkey;
    }

    void onAuthenticationComplete(ble_gap_conn_desc *desc) override {
        PendingActions::blePasskeyHide.store(true, std::memory_order_relaxed);
        if (desc == nullptr)
            return;
        if (ble_link_admitted_rs(desc->sec_state.encrypted, desc->sec_state.authenticated)) {
            LOG_INFO("BLE", "Pairing complete — link authenticated");
            return;
        }
        BleServerInternal::dropUnauthenticatedLink(*desc, "Pairing finished without the passkey");
    }
};

class TimerStateCallbacks : public BleServerInternal::AuthenticatedSubscribeCallbacks {
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

struct CharSpec {
    const char *uuid;
    uint32_t props;
    NimBLECharacteristic **slot;
    NimBLECharacteristicCallbacks *callbacks;
};

constexpr uint32_t kNotifyProps = NIMBLE_PROPERTY::READ_AUTHEN | NIMBLE_PROPERTY::NOTIFY;
constexpr uint32_t kCommandProps = NIMBLE_PROPERTY::WRITE_AUTHEN | NIMBLE_PROPERTY::WRITE_NR;
constexpr uint32_t kSettingsProps = NIMBLE_PROPERTY::READ_AUTHEN | NIMBLE_PROPERTY::WRITE_AUTHEN;
constexpr uint32_t kReadProps = NIMBLE_PROPERTY::READ_AUTHEN;
constexpr uint32_t kMonitorProps = kNotifyProps | NIMBLE_PROPERTY::WRITE_AUTHEN;

bool createAll(NimBLEService *pSvc, const CharSpec *specs, size_t count) {
    bool allCreated = true;
    for (size_t i = 0; i < count; ++i) {
        *specs[i].slot = requireChar(pSvc, specs[i].uuid, specs[i].props);
        allCreated = allCreated && *specs[i].slot != nullptr;
    }
    return allCreated;
}

void publishInitialValues() {
    BleServerInternal::s_pTele->setValue("{}");
    BleServerInternal::emitAlert();
    BleServerInternal::updateStatus();
}

bool createGattCharacteristics(NimBLEService *pSvc) {
    static TimerStateCallbacks s_timerStateCb;
    static BleServerInternal::AuthenticatedSubscribeCallbacks s_subscribeCb;
    NimBLECharacteristic *pSettings = nullptr;
    NimBLECharacteristic *pCmd = nullptr;
    NimBLECharacteristic *pTimerCmd = nullptr;
    NimBLECharacteristic *pPages = nullptr;
    const CharSpec specs[] = {
        {kTeleUuid, kNotifyProps, &BleServerInternal::s_pTele, &s_subscribeCb},
        {kStatusUuid, kNotifyProps, &BleServerInternal::s_pStatus, &s_subscribeCb},
        {kSettingsUuid, kSettingsProps, &pSettings, &BleServerInternal::settingsCallbacks()},
        {kCmdUuid, kCommandProps, &pCmd, &BleServerInternal::cmdCallbacks()},
        {kTimerCmdUuid, kCommandProps, &pTimerCmd, &BleServerInternal::timerCmdCallbacks()},
        {kTimerStateUuid, kNotifyProps, &BleServerInternal::s_pTimerState, &s_timerStateCb},
        {kTimerLapUuid, kNotifyProps, &BleServerInternal::s_pTimerLap, &s_subscribeCb},
        {kAlertUuid, kNotifyProps, &BleServerInternal::s_pAlert, &s_subscribeCb},
        {kPagesUuid, kReadProps, &pPages, &BleServerInternal::pagesCallbacks()},
        {kCanMonitorUuid, kMonitorProps, &BleServerInternal::s_pCanMonitor,
         &BleServerInternal::canMonitorCallbacks()},
    };
    if (!createAll(pSvc, specs, sizeof(specs) / sizeof(specs[0])))
        return false;
    publishInitialValues();
    for (const CharSpec &spec : specs)
        (*spec.slot)->setCallbacks(spec.callbacks);
    BleServerInternal::s_pTimerState->setValue("{\"st\":0,\"el\":0,\"lc\":0,\"sid\":0,\"ver\":0}");
    return true;
}

NimBLEService *createService() {
    static ServerCallbacks s_serverCb;
    NimBLEServer *pServer = NimBLEDevice::createServer();
    if (pServer == nullptr) {
        LOG_ERROR("BLE", "GATT server alloc failed");
        return nullptr;
    }
    pServer->setCallbacks(&s_serverCb);
    NimBLEService *pSvc = pServer->createService(kSvcUuid);
    if (pSvc == nullptr)
        LOG_ERROR("BLE", "GATT service alloc failed");
    return pSvc;
}

bool startAdvertising() {
    NimBLEAdvertising *pAdv = NimBLEDevice::getAdvertising();
    pAdv->addServiceUUID(kSvcUuid);
    pAdv->setName("CANShift");
    pAdv->setScanResponse(false);
    return BleServerInternal::startAdvertisingFast();
}

bool setupGatt() {
    NimBLEService *pSvc = createService();
    if (pSvc == nullptr || !createGattCharacteristics(pSvc))
        return false;
    if (!pSvc->start()) {
        LOG_ERROR("BLE", "GATT service start failed");
        return false;
    }
    if (!startAdvertising())
        return false;
    s_gattInited = true;
    s_enabled = true;
    LOG_INFO("BLE", "Advertising as 'CANShift' — %s",
             NimBLEDevice::getAddress().toString().c_str());
    return true;
}

bool restartAdvertising() {
    if (!BleServerInternal::startAdvertisingFast())
        return false;
    s_enabled = true;
    LOG_INFO("BLE", "BLE advertising restarted — %s",
             NimBLEDevice::getAddress().toString().c_str());
    return true;
}

bool startStack() {
    if (s_gattInited)
        return restartAdvertising();

    if (!s_stackInited) {
        const size_t avail = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
        if (avail < kBleMinHeap) {
            LOG_WARN("BLE", "Insufficient contiguous DRAM for BLE stack (%u B < %u B) — skipped",
                     static_cast<unsigned>(avail), static_cast<unsigned>(kBleMinHeap));
            return false;
        }
        NimBLEDevice::init("CANShift");
        BleServerInternal::applyTxPower();
        NimBLEDevice::setMTU(kBlePreferredMtu);

        NimBLEDevice::setSecurityAuth(true, true, true);
        NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_ONLY);
        s_stackInited = true;
    }

    const size_t heapFree = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    if (heapFree < kBleGattMinHeapBytes) {
        LOG_WARN("BLE", "Heap too low for GATT setup (%u B < %u B) — BLE disabled",
                 static_cast<unsigned>(heapFree), static_cast<unsigned>(kBleGattMinHeapBytes));
        return false;
    }

    return setupGatt();
}

void reportStartFailure() {
    LOG_ERROR("BLE", "BLE stack start failed");
    ErrorStore::push(ERROR_SRC_SYSTEM, "ble_start", "BLE stack start failed");
    BleServer::requestStop();
}

}

void BleServer::earlyInit() {
    const size_t avail = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    if (avail < kBleMinHeap) {
        LOG_WARN("BLE", "Insufficient contiguous DRAM for BLE early init (%u B < %u B)",
                 static_cast<unsigned>(avail), static_cast<unsigned>(kBleMinHeap));
        return;
    }

    NimBLEDevice::init("CANShift");
    BleServerInternal::applyTxPower();

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
    if (s_enabled || s_released)
        return;
    if (!startStack()) {
        reportStartFailure();
    }
}

void BleServer::tick() {
    if (!BleServerInternal::s_connected) {
        BleServerInternal::paceAdvertising();
        return;
    }
    if (!BleServerInternal::linkAdmitted()) {
        BleServerInternal::dropOverduePeer();
        return;
    }
    BleServerInternal::emitTelemetry();
    BleServerInternal::emitAlert();
    BleServerInternal::emitPageChange();
    BleServerInternal::emitTimerSync();
    BleServerInternal::emitCanMonitor();
}

bool BleServer::isConnected() {
    return BleServerInternal::s_connected;
}

#endif
