// ble_telemetry.cpp — TELE characteristic payload builder + emit pump.
//
// Split out of `ble_server.cpp` per the canshift-firmware Medium-Refactor
// entry in #1207 ("BLE transport vs telemetry"). The TELE characteristic
// streams the active signal set as a compact JSON object at ~10 Hz; this TU
// owns the heap-free payload encoding (issue #891) and the per-tick emit
// path called from `BleServer::tick()`.
//
// Lifetime: the TELE characteristic pointer (`s_pTele`) and the connection
// flag (`s_connected`) live in `ble_server.cpp` — this TU snapshots them
// locally per tick to dodge the race against `BleServer::stop()` documented
// in #1283. The 2s STATUS refresh divider lives here because it is part of
// the emit pump, not part of the STATUS encoder.

#include "app_config.h"
#if APP_BLE_ENABLED

    #include "ble_server.h"
    #include "ble_server_internal.h"

    #include "can/signal_map.h"
    #include "diag/logger.h"
    #include "hal/wifi/wifi_ap.h"
    #include "runtime/signal_store.h"
    #include "util/format_float.h"

    #include <NimBLEDevice.h>
    #include <stdint.h>
    #include <stdio.h>
    #include <string.h>

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

// 2s STATUS refresh divider — kept module-static so the emit pump retains
// byte-identical behaviour with the pre-split `tick()`'s function-local
// `static uint8_t s_statusDiv = 0; static bool s_prevApActive = false;`.
// These intentionally persist across stop()/start() cycles (same as before).
uint8_t s_statusDiv = 0;
bool s_prevApActive = false;

} // namespace

namespace BleServerInternal {

void emitTelemetry() {
    // Snapshot the file-scope pointer to a local — BleServer::stop() can null
    // s_pTele on a different task between the entry check and the notify
    // call below. The earlyInit() GATT-preserved path keeps the underlying
    // characteristic object alive for the lifetime of the process, so the
    // snapshot remains valid even if the global is cleared mid-call. Mirrors
    // the snapshot pattern in updateStatus() for s_pStatus (#1283).
    auto *pTele = BleServerInternal::s_pTele;
    if (!pTele)
        return;
    if (!pTele->getSubscribedCount())
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
        pTele->setValue(reinterpret_cast<uint8_t *>(buf), len);
        pTele->notify();
    }

    // Refresh STATUS every 2s; notify if AP state changed (e.g. timeout)
    if (++s_statusDiv >= 20) {
        BleServerInternal::updateStatus();
        s_statusDiv = 0;
        bool apNow = WifiAp::isActive();
        if (apNow != s_prevApActive) {
            s_prevApActive = apNow;
            auto *pStatus = BleServerInternal::s_pStatus;
            if (pStatus && pStatus->getSubscribedCount() > 0)
                pStatus->notify();
        }
    }
}

} // namespace BleServerInternal

#endif // APP_BLE_ENABLED
