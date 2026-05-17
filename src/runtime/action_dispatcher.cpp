// action_dispatcher.cpp — Shared action dispatch for LVGL + GPIO button inputs.
//
// Extracted from `ui/widgets/button_widget.cpp` so the physical-button input
// task in `runtime/input_buttons.cpp` runs the exact same execution path
// (issue #833). Behaviour preserved verbatim — review `button_widget.cpp`
// git history for the rationale behind each branch.

#include "action_dispatcher.h"

#include "app_config.h"
#include "can/can_manager.h"
#include "can_signals_out.h"
#include "config/config_loader.h"
#include "diag/logger.h"
#include "ui/page_manager.h"

#include <lvgl.h>
#include <string.h>

namespace ActionDispatcher {

namespace {

// Standard / extended CAN ID bounds — mirrored from config_loader.cpp.
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

void dispatchNavPage(const CfgButtonAction &a) {
    if (a.pageId[0] == '\0')
        return;
    // Defer navigateTo to the next LVGL tick: it can synchronously free the
    // screen this button lives on (lazy-build path releases the departing
    // page), and the event loop would otherwise return into freed memory.
    // Closes the re-entrant navigation path in #717.
    static char s_pendingNavId[CFG_MAX_ID_LEN];
    strlcpy(s_pendingNavId, a.pageId, sizeof(s_pendingNavId));
    lv_async_call(
        [](void *p) {
            const char *id = static_cast<const char *>(p);
            PageManager::navigateTo(id);
        },
        s_pendingNavId);
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
