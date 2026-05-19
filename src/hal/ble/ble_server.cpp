// ble_server.cpp — BLE GATT server (NimBLE stack)

#include "app_config.h"
#if APP_BLE_ENABLED

    #include "ble_server.h"
    #include "hal/wifi/wifi_ap.h"
    #include "ui/theme_manager.h"
    #include "runtime/signal_store.h"
    #include "can/signal_map.h"
    #include "ui/settings_page.h"
    #include "config/json_reader.h"
    #include "config/rotation_config.h"
    #include "diag/logger.h"
    #include "diag/lvgl_lock_guard.h"
    #include "runtime/track_store.h"
    #include "app_config.h"

    #include <NimBLEDevice.h>
    #include <ArduinoJson.h>
    #include <freertos/semphr.h>
    #include <Arduino.h>
    #include <Preferences.h>
    #include <esp_heap_caps.h>
    #include <atomic>
    #include <string.h>

// ---------------------------------------------------------------------------
// LVGL mutex (defined in main.cpp)
// ---------------------------------------------------------------------------

extern SemaphoreHandle_t g_lvglMutex;

// ---------------------------------------------------------------------------
// UUIDs
// ---------------------------------------------------------------------------

static constexpr char SVC_UUID[] = "4fa0b6a0-0000-0000-0000-000000000001";
static constexpr char TELE_UUID[] = "4fa0b6a0-0000-0000-0000-000000000002";
static constexpr char STATUS_UUID[] = "4fa0b6a0-0000-0000-0000-000000000003";
static constexpr char SETTINGS_UUID[] = "4fa0b6a0-0000-0000-0000-000000000004";
static constexpr char CMD_UUID[] = "4fa0b6a0-0000-0000-0000-000000000005";

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

static constexpr size_t BLE_MIN_HEAP = 50U * 1024U;

// Hard cap on BLE write payloads parsed as JSON (issue #897). Both the
// SETTINGS and CMD characteristics carry tiny control objects — the largest
// real payload today (`{"cmd":"set_day_night","day":true}`) is well under 64
// bytes. 256 leaves comfortable headroom while denying a peer the ability to
// push MTU-sized (≤512) deeply-nested JSON into ArduinoJson's allocator on
// the BLE task. Any oversized write is dropped before `JsonReader::parse`
// touches the buffer.
static constexpr size_t BLE_MAX_WRITE_LEN = 256U;

// s_stackInited: NimBLEDevice::init() has run (stack allocated).
// s_gattInited:  GATT service + characteristics are set up and advertising started.
//                True after earlyInit() or a successful runtime startStack().
static bool s_stackInited = false;
static bool s_gattInited = false;

static NimBLECharacteristic *s_pTele = nullptr;
static NimBLECharacteristic *s_pStatus = nullptr;
static bool s_connected = false;
static bool s_enabled = false;

// Deferred command flags — set by BLE callbacks, consumed by UI task
static std::atomic<bool> s_pendingDayNightToggle{false};
static std::atomic<bool> s_pendingCalibration{false};
static std::atomic<bool> s_pendingCalibrationReset{false};

// Pending explicit day/night set: -1 = none, 0 = night, 1 = day.
// Separate from the toggle flag so old clients (sending toggle_day_night)
// keep working while new clients (sending set_day_night) get idempotent
// behaviour. The UI task prefers the explicit set when both are pending.
static std::atomic<int8_t> s_pendingDayNightSet{-1};

// Pending BLE enable/disable from the settings page: -1 = none, 0 = disable, 1 = enable.
static std::atomic<int8_t> s_pendingEnabled{-1};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace {

void addSignalIfValid(JsonDocument &doc, const char *key, SignalId id) {
    if (SignalStore::isValid(id))
        doc[key] = roundf(SignalStore::read(id) * 10.0f) / 10.0f; // 1 decimal place
}

void updateStatus() {
    if (!s_pStatus)
        return;
    JsonDocument doc;
    doc["ver"] = APP_VERSION_STR;
    doc["can"] = SignalStore::isValid(SignalIds::RPM) ? 1 : 0;
    doc["is_day"] = ThemeManager::isDayMode() ? 1 : 0;
    if (WifiAp::isActive()) {
        doc["ap_ssid"] = WifiAp::getSsid();
        doc["ap_password"] = WifiAp::getPassword();
    }
    char buf[128];
    // ArduinoJson silently truncates when the output buffer is too small;
    // a truncated STATUS payload is invalid JSON and crashes the mobile
    // parser. Detect and skip rather than push junk over the wire (#936).
    const size_t len = serializeJson(doc, buf, sizeof(buf));
    if (len == 0 || len >= sizeof(buf)) {
        LOG_WARN("BLE", "STATUS payload truncated (len=%u, cap=%u) — skipping notify",
                 static_cast<unsigned>(len), static_cast<unsigned>(sizeof(buf)));
        return;
    }
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
// SETTINGS read/write callback
// Payload (read & write): {"brightness":80,"sleep":30}
//
// onRead refreshes the characteristic value just-in-time so the mobile app
// can populate its Settings UI on open without guessing defaults (#26 / #29).
// ---------------------------------------------------------------------------

class SettingsCallbacks : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic *pChar) override {
        std::string val = pChar->getValue();
        if (val.empty())
            return;
        if (val.length() > BLE_MAX_WRITE_LEN) {
            LOG_WARN("BLE", "SETTINGS write %u bytes exceeds cap %u — dropping",
                     static_cast<unsigned>(val.length()), static_cast<unsigned>(BLE_MAX_WRITE_LEN));
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
                RotationConfig::applyAndReboot(rotation); // never returns
            }
        }
    }

    void onRead(NimBLECharacteristic *pChar) override {
        JsonDocument doc;
        doc["brightness"] = SettingsPage::getBrightness();
        char buf[64];
        // Same truncation guard as updateStatus(): never push a half-serialised
        // payload to a subscriber — leave the prior value visible (#936).
        const size_t len = serializeJson(doc, buf, sizeof(buf));
        if (len == 0 || len >= sizeof(buf)) {
            LOG_WARN("BLE",
                     "SETTINGS read payload truncated (len=%u, cap=%u) — keeping prior value",
                     static_cast<unsigned>(len), static_cast<unsigned>(sizeof(buf)));
            return;
        }
        pChar->setValue(buf);
    }
};

// ---------------------------------------------------------------------------
// CMD write callback
// Payload: {"cmd":"start_wifi_ap"} | {"cmd":"reboot"}
// ---------------------------------------------------------------------------

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

        if (strcmp(cmd, "start_wifi_ap") == 0) {
            WifiAp::start();
            LOG_INFO("BLE", "CMD: starting WiFi AP — SSID: %s", WifiAp::getSsid());
            updateStatus();
            if (s_pStatus->getSubscribedCount() > 0)
                s_pStatus->notify();
        } else if (strcmp(cmd, "stop_wifi_ap") == 0) {
            LOG_INFO("BLE", "CMD: stopping WiFi AP");
            WifiAp::stop();
            updateStatus();
            if (s_pStatus->getSubscribedCount() > 0)
                s_pStatus->notify();
        } else if (strcmp(cmd, "toggle_day_night") == 0) {
            // Deferred to UI task — ThemeManager requires LVGL mutex from UI context
            s_pendingDayNightToggle.store(true, std::memory_order_relaxed);
            LOG_INFO("BLE", "CMD: day/night toggle queued");
        } else if (strcmp(cmd, "set_day_night") == 0) {
            // Explicit, idempotent variant. Payload: {"cmd":"set_day_night","day":<bool>}.
            // Deferred to UI task — ThemeManager requires LVGL mutex from UI context.
            JsonVariantConst dayVar = doc["day"];
            if (dayVar.isNull() || !dayVar.is<bool>()) {
                LOG_WARN("BLE", "set_day_night missing 'day' bool — ignoring");
            } else {
                const bool day = dayVar.as<bool>();
                s_pendingDayNightSet.store(day ? 1 : 0, std::memory_order_relaxed);
                LOG_INFO("BLE", "CMD: day/night set queued — %s", day ? "day" : "night");
            }
        } else if (strcmp(cmd, "start_calibration") == 0) {
            // Deferred to UI task — calibrate() is blocking (user taps crosshairs)
            s_pendingCalibration.store(true, std::memory_order_relaxed);
            LOG_INFO("BLE", "CMD: calibration queued");
        } else if (strcmp(cmd, "reset_calibration") == 0) {
            // Deferred to UI task — keeps NVS access on a single task thread,
            // matching the calibrate path. Reset wipes the persisted offsets;
            // next boot falls back to board defaults + first-boot calibration.
            s_pendingCalibrationReset.store(true, std::memory_order_relaxed);
            LOG_INFO("BLE", "CMD: calibration reset queued");
        } else if (strcmp(cmd, "track_state") == 0) {
            // Track-mode telemetry pushed by canshift-mobile. Mirrors the
            // `TrackTelemetrySchema` in canshift-core (#843). Bounds are
            // already enforced TS-side, but we re-clamp the wide ints to
            // the embedded-side struct widths defensively. Issue #844.
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
            delay(100);
            esp_restart();
        } else {
            LOG_WARN("BLE", "Unknown CMD: %s", cmd);
        }
    }
};

} // namespace

namespace {

// Build the GATT service tree and start advertising.
// Called from earlyInit() (pre-lv_init, heap ~100 KB) or from startStack()
// at runtime (heap may be fragmented — guarded by kGattMinHeap check).
static void setupGatt() {
    // Callbacks are stateless dispatchers — file-scope statics so NimBLE can
    // retain the same instances across deinit/init cycles without leaking
    // (issue #883). Function-local statics keep construction lazy and
    // thread-safe (C++11 magic statics).
    static ServerCallbacks s_serverCb;
    static SettingsCallbacks s_settingsCb;
    static CmdCallbacks s_cmdCb;

    NimBLEServer *pServer = NimBLEDevice::createServer();
    pServer->setCallbacks(&s_serverCb);

    NimBLEService *pSvc = pServer->createService(SVC_UUID);

    s_pTele =
        pSvc->createCharacteristic(TELE_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
    s_pTele->setValue("{}");

    s_pStatus =
        pSvc->createCharacteristic(STATUS_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
    // Real values not available yet at earlyInit() call site — set minimal placeholder.
    // BleServer::init() calls updateStatus() once all subsystems are up.
    s_pStatus->setValue("{\"ver\":\"" APP_VERSION_STR "\",\"can\":0,\"is_day\":0}");

    NimBLECharacteristic *pSettings =
        pSvc->createCharacteristic(SETTINGS_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE);
    pSettings->setCallbacks(&s_settingsCb);

    NimBLECharacteristic *pCmd =
        pSvc->createCharacteristic(CMD_UUID, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
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

// Bring up BLE advertising. If GATT was already set up by earlyInit(), simply
// restarts advertising (no heap allocation). Otherwise does a full init with
// a heap guard to prevent bad_alloc on a fragmented post-boot heap.
bool startStack() {
    if (s_gattInited) {
        // earlyInit() already built the GATT tree — just restart advertising.
        NimBLEDevice::getAdvertising()->start();
        s_enabled = true;
        LOG_INFO("BLE", "BLE advertising restarted — %s",
                 NimBLEDevice::getAddress().toString().c_str());
        return true;
    }

    if (!s_stackInited) {
        // earlyInit() did not run (BLE disabled at boot, or runtime re-enable).
        const size_t avail = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
        if (avail < BLE_MIN_HEAP) {
            LOG_WARN("BLE", "Insufficient contiguous DRAM for BLE stack (%u B < %u B) — skipped",
                     static_cast<unsigned>(avail), static_cast<unsigned>(BLE_MIN_HEAP));
            return false;
        }
        NimBLEDevice::init("CANShift");
        NimBLEDevice::setPower(ESP_PWR_LVL_P9);
        s_stackInited = true;
    }

    // GATT setup on fragmented post-boot heap — guard against bad_alloc.
    // 24 KB empirical minimum covers createServer + 4 characteristics + start().
    constexpr size_t kGattMinHeap = 24U * 1024U;
    const size_t heapFree = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    if (heapFree < kGattMinHeap) {
        LOG_WARN("BLE", "Heap too low for GATT setup (%u B < %u B) — BLE disabled",
                 static_cast<unsigned>(heapFree), static_cast<unsigned>(kGattMinHeap));
        return false;
    }

    setupGatt();
    return true;
}

} // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void BleServer::earlyInit() {
    // Read the BLE-enabled preference directly from NVS — SettingsPage (and
    // LVGL) are not yet initialized at this call site.
    Preferences p;
    p.begin("screen_cfg", /*readOnly=*/true);
    const bool enabled = p.getUChar("ble_en", 1) != 0;
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
    s_stackInited = true;
    LOG_INFO("BLE", "BLE stack initialized (%u B available) — building GATT tree",
             static_cast<unsigned>(avail));

    // Build GATT service tree now, while heap is large and unfragmented.
    // After lv_init() claims 80 KB, the remaining heap is too fragmented for
    // NimBLE's C++ new[] allocations (issue confirmed: bad_alloc → terminate).
    setupGatt();
}

void BleServer::init() {
    if (!SettingsPage::getBleEnabled()) {
        LOG_INFO("BLE", "BLE disabled by user setting — skipping init");
        return;
    }
    startStack();
    if (s_enabled) {
        updateStatus();
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
        // GATT was built in earlyInit — stop advertising only. Keeping the GATT
        // tree alive avoids a re-init that would fail on the fragmented post-boot heap.
        NimBLEDevice::stopAdvertising();
        s_connected = false;
        s_enabled = false;
        LOG_INFO("BLE", "BLE advertising stopped (GATT preserved)");
    } else {
        // Runtime-started stack — full deinit to free the heap.
        NimBLEDevice::deinit(true);
        s_pTele = nullptr;
        s_pStatus = nullptr;
        s_connected = false;
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
    if (!s_connected || !s_pTele)
        return;
    if (!s_pTele->getSubscribedCount())
        return;

    JsonDocument doc;
    addSignalIfValid(doc, "r", SignalIds::RPM);
    addSignalIfValid(doc, "tps", SignalIds::THROTTLE_POS);
    addSignalIfValid(doc, "map", SignalIds::MAP_KPA);
    addSignalIfValid(doc, "mi", SignalIds::MAP_NUMBER);
    addSignalIfValid(doc, "bst", SignalIds::BOOST_BAR);
    addSignalIfValid(doc, "iat", SignalIds::IAT_C);
    addSignalIfValid(doc, "ct", SignalIds::COOLANT_TEMP_C);
    addSignalIfValid(doc, "ot", SignalIds::OIL_TEMP_C);
    addSignalIfValid(doc, "op", SignalIds::OIL_PRESS_BAR);
    addSignalIfValid(doc, "fp", SignalIds::FUEL_PRESS_BAR);
    addSignalIfValid(doc, "lam", SignalIds::LAMBDA_1);
    addSignalIfValid(doc, "s", SignalIds::SPEED_KPH);
    addSignalIfValid(doc, "g", SignalIds::GEAR);
    addSignalIfValid(doc, "bat", SignalIds::BATTERY_VOLTS);

    char buf[512];
    // ArduinoJson silently truncates on overflow; pushing a half-serialised
    // payload at 10 Hz over BLE would feed the mobile parser garbage. Skip
    // the notify if the cap is hit so the prior good frame stays visible.
    // STATUS refresh below is independent and must still run (#936).
    const size_t len = serializeJson(doc, buf, sizeof(buf));
    if (len == 0 || len >= sizeof(buf)) {
        LOG_WARN("BLE", "TELE payload truncated (len=%u, cap=%u) — skipping notify",
                 static_cast<unsigned>(len), static_cast<unsigned>(sizeof(buf)));
    } else {
        s_pTele->setValue(reinterpret_cast<uint8_t *>(buf), len);
        s_pTele->notify();
    }

    // Refresh STATUS every 2s; notify if AP state changed (e.g. timeout)
    static uint8_t s_statusDiv = 0;
    static bool s_prevApActive = false;
    if (++s_statusDiv >= 20) {
        updateStatus();
        s_statusDiv = 0;
        bool apNow = WifiAp::isActive();
        if (apNow != s_prevApActive) {
            s_prevApActive = apNow;
            if (s_pStatus->getSubscribedCount() > 0)
                s_pStatus->notify();
        }
    }
}

bool BleServer::isConnected() {
    return s_connected;
}

void BleServer::pushStatusNotify() {
    updateStatus();
    if (s_pStatus && s_pStatus->getSubscribedCount() > 0)
        s_pStatus->notify();
}

bool BleServer::takePendingDayNightToggle() {
    return s_pendingDayNightToggle.exchange(false, std::memory_order_relaxed);
}

int8_t BleServer::takePendingDayNightSet() {
    return s_pendingDayNightSet.exchange(-1, std::memory_order_relaxed);
}

bool BleServer::takePendingCalibration() {
    return s_pendingCalibration.exchange(false, std::memory_order_relaxed);
}

bool BleServer::takePendingCalibrationReset() {
    return s_pendingCalibrationReset.exchange(false, std::memory_order_relaxed);
}

#endif // APP_BLE_ENABLED
