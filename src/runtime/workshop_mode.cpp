#include "app_config.h"
#if APP_WORKSHOP_ENABLED

    #include "workshop_mode.h"

    #include "can/signal_map.h"
    #include "config/config_loader.h"
    #include "diag/error_store.h"
    #include "diag/logger.h"
    #include "hal/storage/nvs_store.h"
    #include "hal/storage/storage_driver.h"
    #include "runtime/signal_store.h"
    #include "runtime/workshop_credentials.h"
    #include "workshop_mode_rs.h"

    #include <Arduino.h>
    #include <esp_system.h>

    #include <atomic>
    #include <stddef.h>
    #include <stdint.h>

namespace WorkshopMode {

static_assert(kNoResumeReason == kWorkshopResumeNone, "no-resume reason drifted");

namespace {

constexpr char kSpeedSignalName[] = "speed_kph";
constexpr uint32_t kNoSpaceInfo = 0;
constexpr float kSpeedWhenUnread = 0.0f;
constexpr char kResumeKey[] = "resume";

std::atomic<uint8_t> s_state{kWorkshopStateDriving};
WorkshopMotionRs s_motion = {};

uint32_t filesystemBytes() {
    if (StorageDriver::getStatus() != StorageDriver::InitStatus::Ok)
        return kNoSpaceInfo;
    size_t total = 0;
    StorageDriver::getSpaceInfo(&total, nullptr);
    return static_cast<uint32_t>(total);
}

uint8_t advance(uint8_t event, bool isOffered) {
    const uint8_t next = workshop_next_state_rs(s_state.load(), event, isOffered);
    s_state.store(next);
    return next;
}

bool disarmResume() {
    if (NvsStore::remove(kWorkshopNvsNamespace, kResumeKey))
        return true;
    LOG_ERROR("WORKSHOP", "Resume flag not cleared — booting into the dash");
    ErrorStore::push(ERROR_SRC_SYSTEM, "nvs_write", "workshop resume not cleared");
    return false;
}

bool motionExitDue(uint32_t nowMs) {
    const bool speedValid = SignalStore::isValid(SignalIds::kSpeedKph);
    const float speedKph = SignalStore::read(SignalIds::kSpeedKph, kSpeedWhenUnread);
    return workshop_motion_exit_due_rs(&s_motion, speedValid, speedKph, nowMs);
}

void armResume(uint8_t reason) {
    if (NvsStore::putUChar(kWorkshopNvsNamespace, kResumeKey, reason)) {
        LOG_INFO("WORKSHOP", "Workshop mode resumes after the reboot");
        return;
    }
    LOG_ERROR("WORKSHOP", "Resume flag not stored — the reboot lands in the dash");
    ErrorStore::push(ERROR_SRC_SYSTEM, "nvs_write", "workshop resume not stored");
}

} // namespace

bool boardSupported() {
    return workshop_board_supported_rs(filesystemBytes());
}

bool offered() {
    const bool speedMapped = ConfigLoader::findSignal(kSpeedSignalName) != nullptr;
    const bool speedValid = SignalStore::isValid(SignalIds::kSpeedKph);
    const float speedKph = SignalStore::readRaw(SignalIds::kSpeedKph, kSpeedWhenUnread);
    return workshop_offered_rs(filesystemBytes(), speedMapped, speedValid, speedKph);
}

bool request() {
    const bool entering = advance(kWorkshopEventRequest, offered()) == kWorkshopStateEntering;
    if (entering)
        LOG_INFO("WORKSHOP", "Workshop mode requested");
    return entering;
}

bool isEntering() {
    return s_state.load() == kWorkshopStateEntering;
}

bool isActive() {
    return s_state.load() == kWorkshopStateActive;
}

void settleEntry(bool entered) {
    const uint8_t event = entered ? kWorkshopEventEntered : kWorkshopEventFailed;
    if (advance(event, false) == kWorkshopStateActive)
        LOG_INFO("WORKSHOP", "Workshop mode active — dash disabled");
}

void armResumeAfterUpdate() {
    armResume(kWorkshopResumeUpdate);
}

void armResumeAfterConfigChange() {
    armResume(kWorkshopResumeConfig);
}

uint8_t takeResumeRequest() {
    const uint8_t reason =
        NvsStore::getUChar(kWorkshopNvsNamespace, kResumeKey, kWorkshopResumeNone);
    if (reason == kWorkshopResumeNone)
        return kWorkshopResumeNone;
    return disarmResume() ? reason : kWorkshopResumeNone;
}

void resumeAfterReboot(uint8_t reason, bool imagePendingVerify) {
    if (!workshop_resume_after_reboot_rs(reason, filesystemBytes(), imagePendingVerify))
        return;
    if (advance(kWorkshopEventResume, false) == kWorkshopStateEntering)
        LOG_INFO("WORKSHOP", "Workshop mode resumed after the reboot");
}

void leaveOnMotion(uint32_t nowMs) {
    if (!isActive() || !motionExitDue(nowMs))
        return;
    LOG_WARN("WORKSHOP", "Car moving in workshop mode — rebooting into the dash");
    ErrorStore::push(ERROR_SRC_SYSTEM, "WS_MOTION", "workshop left: car moving");
    if (!disarmResume())
        LOG_WARN("WORKSHOP",
                 "Resume flag kept — the next boot may resume until the watchdog trips again");
    delay(kPreRestartFlushDelayMs);
    esp_restart();
}

} // namespace WorkshopMode

#endif
