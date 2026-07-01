#include "action_dispatcher.h"

#include "app_config.h"
#include "can/can_manager.h"
#include "can_signals_out.h"
#include "config/config_loader.h"
#include "diag/error_store.h"
#include "diag/logger.h"
#include "runtime/pending_actions.h"

#include <stdio.h>

namespace ActionDispatcher {

namespace {

constexpr uint32_t CAN_STANDARD_ID_MAX = 0x7FFu;

void sendControlFrame(uint32_t frameId, const uint8_t *data, uint8_t len, bool extended,
                      const char *what) {
    if (CanManager::sendFrame(frameId, data, len, extended))
        return;
    LOG_WARN("BTN", "%s: sendFrame id=0x%lX dropped", what, static_cast<unsigned long>(frameId));
    char msg[sizeof(FwError::message)];
    snprintf(msg, sizeof(msg), "%s tx dropped id=0x%lX", what, static_cast<unsigned long>(frameId));
    ErrorStore::push(ERROR_SRC_CAN, "TX_DROP", msg);
}

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

// Never touch LVGL here: this runs in the input task, and lv_async_call
// mutates LVGL's timer list without g_lvglMutex. taskUI drains the slot.
void dispatchNavPage(const CfgButtonAction &a) {
    if (a.pageId[0] == '\0')
        return;
    PendingActions::requestNavPage(a.pageId);
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
    sendControlFrame(frameId, payload, CAN_OUT_MAP_SWITCH_DLC, extended, "map_switch");
}

void dispatchCanRaw(const CfgButtonAction &a, bool isActive) {

    if (!isActive && a.canDataOffLen > 0) {
        sendControlFrame(a.canFrameId, a.canDataOff, a.canDataOffLen, a.canExtended, "can_raw");
    } else {
        sendControlFrame(a.canFrameId, a.canData, a.canDataLen, a.canExtended, "can_raw");
    }
    (void)CAN_STANDARD_ID_MAX;
}

void dispatchCruiseControl(const CfgButtonAction &a) {

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
