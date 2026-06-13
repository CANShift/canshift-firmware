#include "app_config.h"
#if APP_BLE_ENABLED

    #include "ble_server.h"
    #include "ble_server_internal.h"

    #include "can/signal_map.h"
    #include "diag/logger.h"
    #include "runtime/signal_store.h"
    #include "util/format_float.h"

    #include <NimBLEDevice.h>
    #include <stdint.h>
    #include <stdio.h>
    #include <string.h>

namespace {

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

constexpr int BLE_TELE_SIG_DIGITS = 4;

size_t buildTelemetryPayload(char *buf, size_t bufSize) {
    if (bufSize < 4)
        return 0;
    char *p = buf;
    char *const end = buf + bufSize - 1;

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

uint8_t s_statusDiv = 0;

} // namespace

namespace BleServerInternal {

void emitTelemetry() {

    auto *pTele = BleServerInternal::s_pTele;
    if (!pTele)
        return;
    if (!pTele->getSubscribedCount())
        return;

    char buf[512];
    const size_t len = buildTelemetryPayload(buf, sizeof(buf));
    if (len == 0) {
        LOG_WARN("BLE", "TELE payload would overflow %u B buffer — skipping notify",
                 static_cast<unsigned>(sizeof(buf)));
    } else {
        pTele->setValue(reinterpret_cast<uint8_t *>(buf), len);
        pTele->notify();
    }

    if (++s_statusDiv >= 20) {
        BleServerInternal::updateStatus();
        s_statusDiv = 0;
    }
}

} // namespace BleServerInternal

#endif
