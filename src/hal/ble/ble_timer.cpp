#include "app_config.h"
#if APP_BLE_ENABLED

    #include "ble_server.h"
    #include "ble_server_internal.h"

    #include "diag/logger.h"
    #include "runtime/timer_service.h"

    #include <NimBLEDevice.h>
    #include <stdio.h>

namespace {

constexpr size_t TIMER_PAYLOAD_CAP = 96;
constexpr uint8_t MAX_LAP_NOTIFIES_PER_TICK = 4;

uint32_t s_lastNotifiedVersion = 0;
bool s_versionSeeded = false;

size_t buildTimerStatePayload(char *buf, size_t bufSize, const TimerService::Snapshot &snap) {
    const int len =
        snprintf(buf, bufSize, "{\"st\":%u,\"el\":%lu,\"lc\":%u,\"sid\":%u,\"ver\":%lu}",
                 static_cast<unsigned>(snap.state), static_cast<unsigned long>(snap.elapsedMs),
                 static_cast<unsigned>(snap.lapCount), static_cast<unsigned>(snap.sessionId),
                 static_cast<unsigned long>(snap.version));
    if (len <= 0 || static_cast<size_t>(len) >= bufSize)
        return 0;
    return static_cast<size_t>(len);
}

size_t buildTimerLapPayload(char *buf, size_t bufSize, const TimerService::Lap &lap) {
    const int len =
        snprintf(buf, bufSize, "{\"sid\":%u,\"idx\":%u,\"lap_ms\":%lu,\"total_ms\":%lu}",
                 static_cast<unsigned>(lap.sessionId), static_cast<unsigned>(lap.index),
                 static_cast<unsigned long>(lap.lapMs), static_cast<unsigned long>(lap.totalMs));
    if (len <= 0 || static_cast<size_t>(len) >= bufSize)
        return 0;
    return static_cast<size_t>(len);
}

void notifyTimerState(NimBLECharacteristic *pState, const TimerService::Snapshot &snap) {
    char buf[TIMER_PAYLOAD_CAP];
    const size_t len = buildTimerStatePayload(buf, sizeof(buf), snap);
    if (len == 0) {
        LOG_WARN("BLE", "TIMER state payload truncated — skipping notify");
        return;
    }
    pState->setValue(reinterpret_cast<uint8_t *>(buf), len);
    if (pState->getSubscribedCount() > 0)
        pState->notify();
}

void flushPendingLaps(NimBLECharacteristic *pLap) {
    if (pLap->getSubscribedCount() == 0)
        return;

    for (uint8_t i = 0; i < MAX_LAP_NOTIFIES_PER_TICK; i++) {
        TimerService::Lap lap = {};
        if (!TimerService::popPendingLap(lap))
            return;

        char buf[TIMER_PAYLOAD_CAP];
        const size_t len = buildTimerLapPayload(buf, sizeof(buf), lap);
        if (len == 0) {
            LOG_WARN("BLE", "TIMER lap payload truncated — lap %u lost",
                     static_cast<unsigned>(lap.index));
            continue;
        }
        pLap->setValue(reinterpret_cast<uint8_t *>(buf), len);
        pLap->notify();
    }
}

} // namespace

namespace BleServerInternal {

void refreshTimerStateValue() {
    auto *pState = s_pTimerState;
    if (!pState)
        return;
    char buf[TIMER_PAYLOAD_CAP];
    const size_t len = buildTimerStatePayload(buf, sizeof(buf), TimerService::snapshot());
    if (len == 0)
        return;
    pState->setValue(reinterpret_cast<uint8_t *>(buf), len);
}

void emitTimerSync() {
    auto *pState = s_pTimerState;
    auto *pLap = s_pTimerLap;
    if (!pState || !pLap)
        return;

    const TimerService::Snapshot snap = TimerService::snapshot();
    if (!s_versionSeeded || snap.version != s_lastNotifiedVersion) {
        notifyTimerState(pState, snap);
        s_lastNotifiedVersion = snap.version;
        s_versionSeeded = true;
    }

    flushPendingLaps(pLap);
}

} // namespace BleServerInternal

#endif
