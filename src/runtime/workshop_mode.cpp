#include "app_config.h"
#if APP_WORKSHOP_ENABLED

    #include "workshop_mode.h"

    #include "can/signal_map.h"
    #include "config/config_loader.h"
    #include "diag/logger.h"
    #include "hal/storage/storage_driver.h"
    #include "runtime/signal_store.h"
    #include "workshop_mode_rs.h"

    #include <atomic>
    #include <stddef.h>
    #include <stdint.h>

namespace WorkshopMode {

namespace {

constexpr char kSpeedSignalName[] = "speed_kph";
constexpr uint32_t kNoSpaceInfo = 0;
constexpr float kSpeedWhenUnread = 0.0f;

std::atomic<uint8_t> s_state{kWorkshopStateDriving};

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

} // namespace

bool boardSupported() {
    return workshop_board_supported_rs(filesystemBytes());
}

bool offered() {
    const bool speedMapped = ConfigLoader::findSignal(kSpeedSignalName) != nullptr;
    const bool speedValid = SignalStore::isValid(SignalIds::kSpeedKph);
    const float speedKph = SignalStore::read(SignalIds::kSpeedKph, kSpeedWhenUnread);
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

} // namespace WorkshopMode

#endif
