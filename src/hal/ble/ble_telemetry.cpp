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

constexpr SignalId kBleTeleSignals[] = {
    SignalIds::kRpm,          SignalIds::kThrottlePos,  SignalIds::kMapKpa,
    SignalIds::kMapNumber,    SignalIds::kBoostBar,     SignalIds::kIatC,
    SignalIds::kCoolantTempC, SignalIds::kOilTempC,     SignalIds::kOilPressBar,
    SignalIds::kFuelPressBar, SignalIds::kLambda1,      SignalIds::kSpeedKph,
    SignalIds::kGear,         SignalIds::kBatteryVolts,
};

constexpr size_t kBleTeleSignalCount = sizeof(kBleTeleSignals) / sizeof(kBleTeleSignals[0]);

size_t buildTelemetryPayload(uint8_t *buf, size_t bufSize) {
    TelemetryFrame::Field fields[kBleTeleSignalCount];
    for (size_t i = 0; i < kBleTeleSignalCount; i++) {
        fields[i].present = SignalStore::tryReadDisplay(kBleTeleSignals[i], &fields[i].value);
    }
    return TelemetryFrame::encode(fields, kBleTeleSignalCount, buf, bufSize);
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

    uint8_t buf[TelemetryFrame::kMaxFrameBytes];
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
