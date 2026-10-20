#include "app_config.h"
#if APP_SD_DATALOG_ENABLED

    #include "datalog_file.h"
    #include "datalog_shared.h"
    #include "datalog_tasks.h"

    #include "datalog_rs.h"
    #include "diag/error_store.h"
    #include "diag/logger.h"
    #include "hal/storage/nvs_store.h"
    #include "hal/storage/sd_card.h"
    #include "runtime/bus_health.h"
    #include "runtime/workshop_mode.h"

    #include <Arduino.h>
    #include <esp_heap_caps.h>
    #include <esp_task_wdt.h>

static_assert(kDatalogStagingBytes >= kDatalogPreambleBytes + kDatalogMaxHeaderBytes,
              "the staging buffer must hold the file preamble and header");

namespace DatalogTasks {

namespace {

constexpr char kNvsNamespace[] = "datalog";
constexpr char kNvsNextSessionKey[] = "next";
constexpr uint32_t kFirstSession = 1;

struct FailureReport {
    const char *code;
    const char *message;
};

constexpr FailureReport kFailureReports[] = {
    {"", ""},
    {"sd_full", "SD card full, datalog stopped"},
    {"sd_write", "SD write failed, datalog stopped"},
};

static_assert(sizeof(kFailureReports) / sizeof(kFailureReports[0]) ==
                  static_cast<size_t>(DatalogFile::Result::Failed) + 1,
              "every file result needs a report");

uint8_t *s_staging = nullptr;
DatalogSessionRs s_session = {};
uint32_t s_droppedAtOpen = 0;
uint32_t s_lastSyncMs = 0;
uint32_t s_lastMountTryMs = 0;
bool s_mountTried = false;
bool s_absentReported = false;

void halt(DatalogFile::Result result) {
    DatalogShared::endRecording();
    DatalogFile::abandon();
    datalog_session_halt_rs(&s_session);
    const FailureReport &report = kFailureReports[static_cast<size_t>(result)];
    LOG_ERROR("DLOG", "%s", report.message);
    ErrorStore::push(ERROR_SRC_SYSTEM, report.code, report.message);
    if (result == DatalogFile::Result::Failed)
        SdCard::unmount();
}

void requireOk(DatalogFile::Result result) {
    if (result != DatalogFile::Result::Ok)
        halt(result);
}

bool check(DatalogFile::Result result) {
    requireOk(result);
    return result == DatalogFile::Result::Ok;
}

void discardRing() {
    while (DatalogShared::take(s_staging, kDatalogStagingBytes) > 0) {}
}

bool drain() {
    while (true) {
        const size_t len = DatalogShared::take(s_staging, kDatalogStagingBytes);
        if (len == 0)
            return true;
        if (!check(DatalogFile::append(s_staging, len)))
            return false;
    }
}

uint16_t storedNextSession() {
    uint32_t next = kFirstSession;
    if (!NvsStore::getULong(kNvsNamespace, kNvsNextSessionKey, next))
        return kFirstSession;
    return static_cast<uint16_t>(next);
}

void storeNextSession(uint16_t used) {
    if (NvsStore::putULong(kNvsNamespace, kNvsNextSessionKey, static_cast<uint16_t>(used + 1)))
        return;
    LOG_WARN("DLOG", "next session number not persisted");
    ErrorStore::push(ERROR_SRC_SYSTEM, "nvs_write", "datalog session number not persisted");
}

void openSession() {
    s_droppedAtOpen = DatalogShared::dropped();
    discardRing();
    uint16_t session = storedNextSession();
    if (!check(DatalogFile::open(session, s_staging, kDatalogStagingBytes)))
        return;
    storeNextSession(session);
    s_lastSyncMs = millis();
    DatalogShared::beginRecording(s_lastSyncMs);
}

void closeSession() {
    DatalogShared::endRecording();
    const uint32_t elapsedMs = millis() - DatalogShared::startMs();
    if (!drain())
        return;
    const uint32_t dropped = DatalogShared::dropped() - s_droppedAtOpen;
    if (check(DatalogFile::finish(elapsedMs, dropped)))
        LOG_INFO("DLOG", "session closed after %u ms, %u records dropped",
                 static_cast<unsigned>(elapsedMs), static_cast<unsigned>(dropped));
}

void noAction() {}

constexpr void (*kActions[])() = {noAction, openSession, closeSession};

static_assert(sizeof(kActions) / sizeof(kActions[0]) == kDatalogActionClose + 1,
              "every session action needs a handler");

void runAction(uint8_t action) {
    if (action >= sizeof(kActions) / sizeof(kActions[0]))
        return;
    kActions[action]();
}

void tickMount(uint32_t nowMs) {
    if (SdCard::mounted())
        return;
    if (s_mountTried && nowMs - s_lastMountTryMs < kDatalogMountRetryMs)
        return;
    s_mountTried = true;
    s_lastMountTryMs = nowMs;
    if (SdCard::mount() || s_absentReported)
        return;
    s_absentReported = true;
    ErrorStore::push(ERROR_SRC_SYSTEM, "sd_absent", "no SD card, datalog off");
}

DatalogSessionInputsRs readInputs() {
    const BusHealth::State bus = BusHealth::sample();
    DatalogSessionInputsRs inputs = {};
    inputs.bus_silent = bus.silent;
    inputs.silent_seconds = bus.seconds;
    inputs.card_ready = SdCard::mounted();
    inputs.workshop = WorkshopMode::isEntering() || WorkshopMode::isActive();
    return inputs;
}

void flushRecording(uint32_t nowMs) {
    if (!DatalogShared::recording() || !drain())
        return;
    if (nowMs - s_lastSyncMs < kDatalogSyncIntervalMs)
        return;
    s_lastSyncMs = nowMs;
    requireOk(DatalogFile::sync());
}

}

bool reserveStaging() {
    s_staging = static_cast<uint8_t *>(
        heap_caps_malloc(kDatalogStagingBytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
    if (s_staging == nullptr)
        LOG_ERROR("DLOG", "no DMA memory for the %u B datalog staging buffer",
                  static_cast<unsigned>(kDatalogStagingBytes));
    return s_staging != nullptr;
}

void writerTask(void *) {
    while (true) {
        const uint32_t nowMs = millis();
        tickMount(nowMs);
        const DatalogSessionInputsRs inputs = readInputs();
        runAction(datalog_session_step_rs(&s_session, &inputs));
        flushRecording(nowMs);
        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(kDatalogWriterTickMs));
    }
}

}

#endif
