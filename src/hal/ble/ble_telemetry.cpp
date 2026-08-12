#include "app_config.h"
#if APP_BLE_ENABLED

    #include "ble_server.h"
    #include "ble_server_internal.h"

    #include "can/signal_map.h"
    #include "diag/logger.h"
    #include "hal/ble/telemetry_frame.h"
    #include "runtime/signal_store.h"

    #include <NimBLEDevice.h>
    #include <stddef.h>
    #include <stdint.h>

namespace {

constexpr SignalId BLE_TELE_SIGNALS[] = {
    SignalIds::RPM,
    SignalIds::THROTTLE_POS,
    SignalIds::MAP_KPA,
    SignalIds::MAP_NUMBER,
    SignalIds::BOOST_BAR,
    SignalIds::IAT_C,
    SignalIds::COOLANT_TEMP_C,
    SignalIds::OIL_TEMP_C,
    SignalIds::OIL_PRESS_BAR,
    SignalIds::FUEL_PRESS_BAR,
    SignalIds::LAMBDA_1,
    SignalIds::SPEED_KPH,
    SignalIds::GEAR,
    SignalIds::BATTERY_VOLTS,
};

constexpr size_t BLE_TELE_SIGNAL_COUNT = sizeof(BLE_TELE_SIGNALS) / sizeof(BLE_TELE_SIGNALS[0]);

size_t buildTelemetryPayload(uint8_t *buf, size_t bufSize) {
    static SignalStore::SignalValue snap[SIGNAL_STORE_MAX_SIGNALS];
    SignalStore::snapshotAll(snap);

    TelemetryFrame::Field fields[BLE_TELE_SIGNAL_COUNT];
    for (size_t i = 0; i < BLE_TELE_SIGNAL_COUNT; i++) {
        const SignalId id = BLE_TELE_SIGNALS[i];
        fields[i].present = snap[id].valid;
        fields[i].value = snap[id].smoothed;
    }
    return TelemetryFrame::encode(fields, BLE_TELE_SIGNAL_COUNT, buf, bufSize);
}

uint8_t s_statusDiv = 0;

TelemetryFrame::Deduper s_dedup = {};

size_t s_lastSubCount = 0;

} // namespace

namespace BleServerInternal {

void emitTelemetry() {

    auto *pTele = BleServerInternal::s_pTele;
    if (!pTele)
        return;
    const size_t subs = pTele->getSubscribedCount();
    if (subs == 0) {
        s_lastSubCount = 0;
        return;
    }
    if (subs > s_lastSubCount)
        TelemetryFrame::resetDeduper(s_dedup);
    s_lastSubCount = subs;

    uint8_t buf[TelemetryFrame::MAX_FRAME_BYTES];
    const size_t len = buildTelemetryPayload(buf, sizeof(buf));
    if (len == 0) {
        LOG_WARN("BLE", "TELE payload would overflow %u B buffer — skipping notify",
                 static_cast<unsigned>(sizeof(buf)));
    } else if (TelemetryFrame::shouldSend(s_dedup, buf, len)) {
        pTele->setValue(buf, len);
        pTele->notify();
    }

    if (++s_statusDiv >= 20) {
        BleServerInternal::updateStatus();
        s_statusDiv = 0;
    }
}

} // namespace BleServerInternal

#endif
