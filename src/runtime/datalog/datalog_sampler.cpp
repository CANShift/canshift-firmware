#include "app_config.h"
#if APP_SD_DATALOG_ENABLED

    #include "datalog_shared.h"
    #include "datalog_tasks.h"

    #include "datalog_rs.h"
    #include "runtime/signal_store.h"
    #include "runtime/timer_service.h"

    #include <Arduino.h>
    #include <esp_task_wdt.h>

static_assert(kSignalStoreMaxSignals == kDatalogSignalSlots,
              "a datalog sample carries every signal store slot");

namespace DatalogTasks {

namespace {

SignalStore::SignalValue s_snapshot[kSignalStoreMaxSignals];
float s_values[kDatalogSignalSlots];
uint8_t s_record[kDatalogMaxSampleBytes];
DatalogLapWatchRs s_laps = {};
bool s_wasRecording = false;

uint64_t captureValues() {
    SignalStore::snapshotAll(s_snapshot);
    uint64_t mask = 0;
    for (size_t slot = 0; slot < kSignalStoreMaxSignals; slot++) {
        s_values[slot] = s_snapshot[slot].raw;
        if (s_snapshot[slot].valid)
            mask |= 1ULL << slot;
    }
    return mask;
}

void pushSample(uint32_t tMs) {
    const uint64_t mask = captureValues();
    DatalogShared::enqueue(s_record,
                           datalog_sample_rs(s_record, sizeof(s_record), tMs, mask, s_values));
}

void pushLap(uint32_t tMs) {
    const TimerService::Snapshot timer = TimerService::snapshot();
    if (!datalog_lap_observe_rs(&s_laps, timer.lapCount, timer.sessionId))
        return;
    DatalogShared::enqueue(
        s_record, datalog_lap_rs(s_record, sizeof(s_record), tMs, timer.lapCount, timer.lastLapMs));
}

void rebaseLaps() {
    const TimerService::Snapshot timer = TimerService::snapshot();
    datalog_lap_rebase_rs(&s_laps, timer.lapCount, timer.sessionId);
}

void sampleOnce() {
    const bool recording = DatalogShared::recording();
    if (recording && !s_wasRecording)
        rebaseLaps();
    s_wasRecording = recording;
    if (!recording)
        return;
    const uint32_t tMs = millis() - DatalogShared::startMs();
    pushSample(tMs);
    pushLap(tMs);
}

}

void samplerTask(void *) {
    TickType_t lastWake = xTaskGetTickCount();
    while (true) {
        sampleOnce();
        esp_task_wdt_reset();
        vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(kDatalogSamplePeriodMs));
    }
}

}

#endif
