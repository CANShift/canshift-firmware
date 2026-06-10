#include "action_dispatcher.h"

#include "app_config.h"
#include "can/can_manager.h"
#include "can_signals_out.h"
#include "config/config_loader.h"
#include "diag/logger.h"
#include "ui/page_manager.h"

#include <lvgl.h>
#include <string.h>

#include <atomic>
#include <cstddef>

namespace ActionDispatcher {

namespace {

// Mirrored from config_loader.cpp.
constexpr uint32_t CAN_STANDARD_ID_MAX = 0x7FFu;

const char *cruiseOpName(CfgCruiseOp op) {
    switch (op) {
        case CfgCruiseOp::ON:
            return "on";
        case CfgCruiseOp::OFF:
            return "off";
        case CfgCruiseOp::TOGGLE:
            return "toggle";
        case CfgCruiseOp::SET:
            return "set";
        case CfgCruiseOp::RESUME:
            return "resume";
        case CfgCruiseOp::INCREMENT:
            return "increment";
        case CfgCruiseOp::DECREMENT:
            return "decrement";
        case CfgCruiseOp::UNKNOWN:
        default:
            return "unknown";
    }
}

// Per-dispatch slot prevents the UI-task vs input-task race that would let
// the second strlcpy overwrite the first before lv_async_call dereferences
// it. NAV_RING_SIZE matches LV_ASYNC_CALL_SIZE so the ring never wraps before
// the callback fires (#876).
constexpr size_t NAV_RING_SIZE = 8;
struct NavSlot {
    char pageId[CFG_MAX_ID_LEN];
};
NavSlot s_navRing[NAV_RING_SIZE];
std::atomic<size_t> s_navHead{0};

void dispatchNavPage(const CfgButtonAction &a) {
    if (a.pageId[0] == '\0')
        return;
    // navigateTo can synchronously free the current screen — defer via
    // lv_async_call so the event loop doesn't return into freed memory (#717).
    const size_t idx = s_navHead.fetch_add(1, std::memory_order_relaxed) % NAV_RING_SIZE;
    char *slot = s_navRing[idx].pageId;
    strlcpy(slot, a.pageId, CFG_MAX_ID_LEN);
    lv_async_call(
        [](void *p) {
            const char *id = static_cast<const char *>(p);
            PageManager::navigateTo(id);
        },
        slot);
}

void dispatchMapSwitch(const CfgButtonAction &a) {
    const CfgSignalsOut &outCfg = ConfigLoader::getSignalConfig().out;
    const uint32_t frameId =
        outCfg.mapSwitchFrameId != 0 ? outCfg.mapSwitchFrameId : CAN_OUT_MAP_SWITCH_ID;
    const bool extended = outCfg.mapSwitchExtended;
    static bool s_warnedMapSwitchUnverified = false;
    if (!s_warnedMapSwitchUnverified) {
        LOG_WARN("BTN", "map_switch frame id=0x%lX (ext=%d) is UNVERIFIED — confirm against ECU",
                 static_cast<unsigned long>(frameId), extended ? 1 : 0);
        s_warnedMapSwitchUnverified = true;
    }
    if (a.mapIndex < MAP_SWITCH_MIN_INDEX || a.mapIndex > MAP_SWITCH_MAX_INDEX) {
        LOG_WARN("BTN", "map_switch: mapIndex=%u out of range [%u,%u] — dropped",
                 static_cast<unsigned>(a.mapIndex), static_cast<unsigned>(MAP_SWITCH_MIN_INDEX),
                 static_cast<unsigned>(MAP_SWITCH_MAX_INDEX));
        return;
    }
    const uint8_t payload[CAN_OUT_MAP_SWITCH_DLC] = {a.mapIndex};
    (void)CanManager::sendFrame(frameId, payload, CAN_OUT_MAP_SWITCH_DLC, extended);
}

void dispatchCanRaw(const CfgButtonAction &a, bool isActive) {
    // When disarming a toggle and a dataOff payload was configured, send the
    // off-frame instead of the arm-frame.
    if (!isActive && a.canDataOffLen > 0) {
        (void)CanManager::sendFrame(a.canFrameId, a.canDataOff, a.canDataOffLen, a.canExtended);
    } else {
        (void)CanManager::sendFrame(a.canFrameId, a.canData, a.canDataLen, a.canExtended);
    }
    (void)CAN_STANDARD_ID_MAX;
}

void dispatchCruiseControl(const CfgButtonAction &a) {
    // Real CAN integration lands with #451 (cruise-control screen) once
    // per-ECU support is confirmed. For now we log the request so the user
    // can verify their physical buttons fire the right op — this also keeps
    // the dispatch contract uniform across all action variants.
    LOG_INFO("BTN", "cruise_control op=%s stepKmh=%u (firmware integration pending #451)",
             cruiseOpName(a.cruiseOp), static_cast<unsigned>(a.cruiseStepKmh));
}

} // namespace

void dispatchAction(const CfgButtonAction &a, bool isActive) {
    switch (a.type) {
        case CfgButtonActionType::NAV_PAGE:
            dispatchNavPage(a);
            break;
        case CfgButtonActionType::MAP_SWITCH:
            dispatchMapSwitch(a);
            break;
        case CfgButtonActionType::CAN_RAW:
            dispatchCanRaw(a, isActive);
            break;
        case CfgButtonActionType::CRUISE_CONTROL:
            dispatchCruiseControl(a);
            break;
        case CfgButtonActionType::UNKNOWN:
        default:
            break;
    }
}

} // namespace ActionDispatcher
