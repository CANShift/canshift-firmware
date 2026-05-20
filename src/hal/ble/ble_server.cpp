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
    #include "runtime/pending_actions.h"
    #include "util/format_float.h"
    #include "app_config.h"

    #include <NimBLEDevice.h>
    #include <ArduinoJson.h>
    #include <freertos/semphr.h>
    #include <Arduino.h>
    #include <Preferences.h>
    #include <esp_heap_caps.h>
    #include <esp_random.h>
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
// AP password lives on a dedicated, encryption-gated characteristic instead
// of riding the STATUS notification. Without this split, the WiFi AP password
// was broadcast over BLE in cleartext to any subscriber within ~30 m (#873).
static constexpr char AP_PWD_UUID[] = "4fa0b6a0-0000-0000-0000-000000000006";

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

// Pre-init heap floor — promoted to app_config.h so board profiles can override.
static constexpr size_t BLE_MIN_HEAP = BLE_MIN_HEAP_BYTES;

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

// Pending BLE enable/disable from the settings page: -1 = none, 0 = disable, 1 = enable.
// Stays local to ble_server — it's BLE-stack lifecycle state, not a user
// command channelable through other transports. Day/night + calibration
// pending flags live in `runtime/pending_actions.h` (shared with USB).
static std::atomic<int8_t> s_pendingEnabled{-1};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace {

// ---------------------------------------------------------------------------
// Stack-only telemetry serializer (issue #891)
// ---------------------------------------------------------------------------
//
// The previous code built a JsonDocument every 100 ms then serialized to a
// stack buffer. ArduinoJson v7's JsonDocument is heap-backed despite the name,
// so the BLE task was running a small malloc/free cycle 10× per second
// against the same post-LVGL heap that #555/#576/#664 work so hard to keep
// unfragmented. Mirrors the heap-free pattern already in use by USB's
// `sendTelemetry()` (`usb_comm.cpp::sendTelemetry`).

struct BleTeleEntry {
    SignalId id;
    const char *key;
};

constexpr BleTeleEntry BLE_TELE_SIGNALS[] = {
    {SignalIds::RPM, "r"},
    {SignalIds::THROTTLE_POS, "tps"},
    {SignalIds::MAP_KPA, "map"},
    {SignalIds::MAP_NUMBER, "mi"},
    {SignalIds::BOOST_BAR, "bst"},
    {SignalIds::IAT_C, "iat"},
    {SignalIds::COOLANT_TEMP_C, "ct"},
    {SignalIds::OIL_TEMP_C, "ot"},
    {SignalIds::OIL_PRESS_BAR, "op"},
    {SignalIds::FUEL_PRESS_BAR, "fp"},
    {SignalIds::LAMBDA_1, "lam"},
    {SignalIds::SPEED_KPH, "s"},
    {SignalIds::GEAR, "g"},
    {SignalIds::BATTERY_VOLTS, "bat"},
};

constexpr size_t BLE_TELE_SIGNAL_COUNT = sizeof(BLE_TELE_SIGNALS) / sizeof(BLE_TELE_SIGNALS[0]);

// 4 significant digits keeps every BLE-emitted signal (max range ~9000 for
// RPM, 1 decimal of precision elsewhere) representable without loss while
// stripping trailing zeros on whole values. Matches the JSON contract the
// mobile parser already accepts — only the trailing-zero behaviour changes
// (12.0 stays "12" instead of becoming "12.0").
constexpr int BLE_TELE_SIG_DIGITS = 4;

// Returns the number of bytes written to `buf` (excluding the null
// terminator). Returns 0 on overflow.
size_t buildTelemetryPayload(char *buf, size_t bufSize) {
    if (bufSize < 4)
        return 0;
    char *p = buf;
    char *const end = buf + bufSize - 1; // reserve \0

    *p++ = '{';
    bool first = true;
    for (size_t i = 0; i < BLE_TELE_SIGNAL_COUNT; i++) {
        if (!SignalStore::isValid(BLE_TELE_SIGNALS[i].id))
            continue;
        const float val = SignalStore::read(BLE_TELE_SIGNALS[i].id);

        if (!first) {
            if (p >= end)
                return 0;
            *p++ = ',';
        }
        first = false;

        const int keyN =
            snprintf(p, static_cast<size_t>(end - p), "\"%s\":", BLE_TELE_SIGNALS[i].key);
        if (keyN <= 0 || p + keyN >= end)
            return 0;
        p += keyN;

        char numBuf[16];
        const size_t numLen =
            FloatFormat::formatGeneral(numBuf, sizeof(numBuf), val, BLE_TELE_SIG_DIGITS);
        if (numLen == 0 || p + numLen >= end)
            return 0;
        memcpy(p, numBuf, numLen);
        p += numLen;
    }
    if (p >= end)
        return 0;
    *p++ = '}';
    *p = '\0';
    return static_cast<size_t>(p - buf);
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
        // ap_password intentionally NOT included here — STATUS notifies are
        // visible to every subscriber, and the password used to ride that
        // stream in cleartext (#873). Paired+encrypted mobile clients read
        // the password by GATTing the dedicated AP_PWD characteristic.
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
        // Tear down any lingering passkey overlay so the dashboard returns
        // immediately if pairing was abandoned mid-flow.
        PendingActions::blePasskeyHide.store(true, std::memory_order_relaxed);
        NimBLEDevice::startAdvertising();
    }

    // Called by NimBLE when a central initiates pairing and we are configured
    // with IOCap DISPLAY_ONLY. We mint a fresh 6-digit code per pairing
    // attempt, hand it to the UI task for on-screen display, and return it to
    // NimBLE so the central can match the value the user types in the mobile
    // app (issue #873).
    uint32_t onPassKeyRequest() override {
        const uint32_t passkey = esp_random() % 1000000u;
        LOG_INFO("BLE", "Pairing requested — passkey %06u", static_cast<unsigned>(passkey));
        // The atomic carries the passkey itself; 0 is reserved as "nothing
        // pending", so on the rare 0 draw we bump to a non-zero value rather
        // than swallow the show request. The user-visible passkey from
        // `PasskeyOverlay::show()` is taken modulo 1_000_000 so the bump
        // never lifts us into a 7-digit code.
        const uint32_t carrier = (passkey == 0u) ? 1u : passkey;
        PendingActions::blePasskeyShow.store(carrier, std::memory_order_relaxed);
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
// AP_PWD read callback
// Returns the current WiFi AP password on demand. The characteristic is
// declared `READ_ENC`, so NimBLE refuses unencrypted reads at the stack
// level — only a paired+bonded mobile client can resolve this read.
// Returns an empty string when the AP is not currently active so a paired
// client can poll the characteristic without having to subscribe to STATUS.
// (#873)
// ---------------------------------------------------------------------------

class ApPasswordCallbacks : public NimBLECharacteristicCallbacks {
    void onRead(NimBLECharacteristic *pChar) override {
        if (WifiAp::isActive()) {
            pChar->setValue(WifiAp::getPassword());
        } else {
            pChar->setValue("");
        }
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
            // s_pStatus is null when GATT setup failed (heap-too-low) or after
            // a runtime BleServer::stop() — guard before notifying (#1007).
            if (s_pStatus && s_pStatus->getSubscribedCount() > 0)
                s_pStatus->notify();
        } else if (strcmp(cmd, "stop_wifi_ap") == 0) {
            LOG_INFO("BLE", "CMD: stopping WiFi AP");
            WifiAp::stop();
            updateStatus();
            if (s_pStatus && s_pStatus->getSubscribedCount() > 0)
                s_pStatus->notify();
        } else if (strcmp(cmd, "toggle_day_night") == 0) {
            // Deferred to UI task — ThemeManager requires LVGL mutex from UI context
            PendingActions::dayNightToggle.store(true, std::memory_order_relaxed);
            LOG_INFO("BLE", "CMD: day/night toggle queued");
        } else if (strcmp(cmd, "set_day_night") == 0) {
            // Explicit, idempotent variant. Payload: {"cmd":"set_day_night","day":<bool>}.
            // Deferred to UI task — ThemeManager requires LVGL mutex from UI context.
            JsonVariantConst dayVar = doc["day"];
            if (dayVar.isNull() || !dayVar.is<bool>()) {
                LOG_WARN("BLE", "set_day_night missing 'day' bool — ignoring");
            } else {
                const bool day = dayVar.as<bool>();
                PendingActions::dayNightSet.store(day ? 1 : 0, std::memory_order_relaxed);
                LOG_INFO("BLE", "CMD: day/night set queued — %s", day ? "day" : "night");
            }
        } else if (strcmp(cmd, "start_calibration") == 0) {
            // Deferred to UI task — calibrate() is blocking (user taps crosshairs)
            PendingActions::touchCalibrate.store(true, std::memory_order_relaxed);
            LOG_INFO("BLE", "CMD: calibration queued");
        } else if (strcmp(cmd, "reset_calibration") == 0) {
            // Deferred to UI task — keeps NVS access on a single task thread,
            // matching the calibrate path. Reset wipes the persisted offsets;
            // next boot falls back to board defaults + first-boot calibration.
            PendingActions::touchCalibrationReset.store(true, std::memory_order_relaxed);
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
    static ApPasswordCallbacks s_apPwdCb;

    NimBLEServer *pServer = NimBLEDevice::createServer();
    pServer->setCallbacks(&s_serverCb);

    NimBLEService *pSvc = pServer->createService(SVC_UUID);

    // All characteristics require an encrypted link for reads/writes. Pre-#873
    // the GATT surface was wide-open: any BLE-capable device within range
    // could subscribe to telemetry, read the WiFi AP password, and write
    // arbitrary commands. The `_ENC` permissions push that gate into the
    // NimBLE stack itself so callbacks never fire on unauthenticated peers.
    //
    // NOTIFY is intentionally NOT paired with `_ENC` — the encryption check
    // happens when the peer writes the CCCD to subscribe (CCCD inherits the
    // characteristic's permissions), so the notification stream is gated at
    // subscribe time without needing a non-standard flag here.
    s_pTele =
        pSvc->createCharacteristic(TELE_UUID, NIMBLE_PROPERTY::READ_ENC | NIMBLE_PROPERTY::NOTIFY);
    s_pTele->setValue("{}");

    s_pStatus = pSvc->createCharacteristic(STATUS_UUID,
                                           NIMBLE_PROPERTY::READ_ENC | NIMBLE_PROPERTY::NOTIFY);
    // Real values not available yet at earlyInit() call site — set minimal placeholder.
    // BleServer::init() calls updateStatus() once all subsystems are up.
    s_pStatus->setValue("{\"ver\":\"" APP_VERSION_STR "\",\"can\":0,\"is_day\":0}");

    NimBLECharacteristic *pSettings = pSvc->createCharacteristic(
        SETTINGS_UUID, NIMBLE_PROPERTY::READ_ENC | NIMBLE_PROPERTY::WRITE_ENC);
    pSettings->setCallbacks(&s_settingsCb);

    // WRITE_NR (write-without-response) is kept for low-latency commands
    // (e.g. `track_state` from canshift-mobile) but the matching `_ENC`
    // permission applies to both response and no-response paths because the
    // permission lives on the attribute, not the GATT op.
    NimBLECharacteristic *pCmd = pSvc->createCharacteristic(
        CMD_UUID, NIMBLE_PROPERTY::WRITE_ENC | NIMBLE_PROPERTY::WRITE_NR);
    pCmd->setCallbacks(&s_cmdCb);

    NimBLECharacteristic *pApPwd =
        pSvc->createCharacteristic(AP_PWD_UUID, NIMBLE_PROPERTY::READ_ENC);
    pApPwd->setCallbacks(&s_apPwdCb);
    pApPwd->setValue("");

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
        // bond=true: persist the LTK across reboots so a paired phone
        // reconnects without re-prompting.
        // mitm=true + sc=true: require Secure Connections with man-in-the-middle
        // protection (passkey entry), defeating the "open GATT to anyone in
        // range" surface that #873 documents.
        NimBLEDevice::setSecurityAuth(true, true, true);
        NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_ONLY);
        s_stackInited = true;
    }

    // GATT setup on fragmented post-boot heap — guard against bad_alloc.
    // BLE_GATT_MIN_HEAP_BYTES is the empirical minimum that covers
    // createServer + 4 characteristics + start() (promoted to app_config.h
    // so board profiles can override).
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

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void BleServer::earlyInit() {
    // Read the BLE-enabled preference directly from NVS — SettingsPage (and
    // LVGL) are not yet initialized at this call site. Default tracks
    // `BLE_DEFAULT_ENABLED` (app_config.h) — off by default so a fresh device
    // doesn't advertise an unauthenticated GATT surface (issue #878).
    Preferences p;
    p.begin("screen_cfg", /*readOnly=*/true);
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
    // Same Secure-Connections + bonding + MITM (passkey entry) configuration
    // as the runtime-init path in startStack() — see that branch for rationale
    // (issue #873).
    NimBLEDevice::setSecurityAuth(true, true, true);
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_ONLY);
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

    // Heap-free telemetry — `buildTelemetryPayload` writes directly to a
    // stack buffer using `snprintf` + `FloatFormat::formatGeneral`, avoiding
    // the per-tick JsonDocument malloc/free that previously hammered the
    // post-LVGL heap at 10 Hz (#891).
    char buf[512];
    const size_t len = buildTelemetryPayload(buf, sizeof(buf));
    if (len == 0) {
        LOG_WARN("BLE", "TELE payload would overflow %u B buffer — skipping notify",
                 static_cast<unsigned>(sizeof(buf)));
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
            if (s_pStatus && s_pStatus->getSubscribedCount() > 0)
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

#endif // APP_BLE_ENABLED
