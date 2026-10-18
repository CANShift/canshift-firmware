#include "device_commands.h"

#include "diag/logger.h"
#include "hal/ble/alert_frame.h"
#include "hal/usb/usb_comm.h"
#include "runtime/pending_actions.h"
#include "runtime/track_store.h"
#include "ui/alert_takeover.h"

#include <string.h>

namespace DeviceCommands {

namespace {

constexpr uint16_t kMaxLapNumber = 9999;

Outcome toggleDayNight(const JsonObjectConst &) {
    PendingActions::dayNightToggle.store(true, std::memory_order_relaxed);
    LOG_INFO("CMD", "day/night toggle queued");
    return Outcome::Ok;
}

Outcome setDayNight(const JsonObjectConst &obj) {
    JsonVariantConst dayVar = obj["day"];
    if (dayVar.isNull() || !dayVar.is<bool>()) {
        LOG_WARN("CMD", "set_day_night missing 'day' bool");
        return Outcome::MissingDayField;
    }
    const bool day = dayVar.as<bool>();
    PendingActions::dayNightSet.store(day ? 1 : 0, std::memory_order_relaxed);
    LOG_INFO("CMD", "day/night set queued — %s", day ? "day" : "night");
    return Outcome::Ok;
}

Outcome startCalibration(const JsonObjectConst &) {
    PendingActions::touchCalibrate.store(true, std::memory_order_relaxed);
    LOG_INFO("CMD", "calibration queued");
    return Outcome::Ok;
}

Outcome resetCalibration(const JsonObjectConst &) {
    PendingActions::touchCalibrationReset.store(true, std::memory_order_relaxed);
    LOG_INFO("CMD", "calibration reset queued");
    return Outcome::Ok;
}

Outcome requestReboot(const JsonObjectConst &) {
    LOG_INFO("CMD", "reboot requested");
    return Outcome::RebootRequested;
}

Outcome acknowledgeAlert(const JsonObjectConst &obj) {
    JsonVariantConst sourceVar = obj["source"];
    if (sourceVar.isNull()) {
        AlertTakeover::requestAcknowledge();
        LOG_INFO("CMD", "critical alert acknowledged");
        return Outcome::Ok;
    }
    const int source = sourceVar.is<int>() ? sourceVar.as<int>() : -1;
    if (source < 0 || source >= AlertFrame::kSourceCount) {
        LOG_WARN("CMD", "ack_alert 'source' is not a known alert source");
        return Outcome::InvalidAlertSource;
    }
    AlertTakeover::requestAcknowledgeSource(static_cast<uint8_t>(source));
    LOG_INFO("CMD", "critical alert acknowledged on source %d", source);
    return Outcome::Ok;
}

Outcome applyTrackState(const JsonObjectConst &obj) {
    TrackStore::State next = {};
    next.trackMode = obj["trackMode"] | false;
    next.currentLapMs = obj["currentLapMs"] | 0;
    next.lastLapMs = obj["lastLapMs"] | 0;
    next.bestLapMs = obj["bestLapMs"] | 0;
    const int lapNum = obj["lapNumber"] | 0;
    next.lapNumber =
        (lapNum < 0) ? 0 : static_cast<uint16_t>(lapNum > kMaxLapNumber ? kMaxLapNumber : lapNum);
    next.deltaMs = obj["deltaMs"] | 0;
    next.isBestLap = obj["isBestLap"] | false;
    TrackStore::setTelemetry(next);
    return Outcome::Ok;
}

constexpr Command kCommands[] = {
    {UsbComm::kCmdToggleDayNight, "toggle_day_night", &toggleDayNight},
    {UsbComm::kCmdSetDayNight, "set_day_night", &setDayNight},
    {UsbComm::kCmdCalibrateTouch, "start_calibration", &startCalibration},
    {UsbComm::kCmdResetTouchCal, "reset_calibration", &resetCalibration},
    {UsbComm::kCmdReboot, "reboot", &requestReboot},
    {kNoUsbCode, "track_state", &applyTrackState},
    {kNoUsbCode, "ack_alert", &acknowledgeAlert},
};

}

const Command *findByUsbCode(uint8_t code) {
    if (code == kNoUsbCode)
        return nullptr;
    for (const Command &c : kCommands) {
        if (c.usbCode == code)
            return &c;
    }
    return nullptr;
}

const Command *findByBleName(const char *name) {
    if (name == nullptr || name[0] == '\0')
        return nullptr;
    for (const Command &c : kCommands) {
        if (strcmp(c.bleName, name) == 0)
            return &c;
    }
    return nullptr;
}

}
