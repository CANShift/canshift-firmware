#include "usb_telemetry.h"
#include "usb_comm.h"

#include "app_config.h"
#include "can/signal_map.h"
#include "runtime/signal_store.h"
#include "util/format_float.h"

#include <stdio.h>
#include <string.h>

namespace {

constexpr size_t kTeleBufSize = 768;

struct TeleEntry {
    SignalId id;
    const char *name;
};

const TeleEntry kTeleSignals[] = {
    {SignalIds::kRpm, "rpm"},
    {SignalIds::kThrottlePos, "throttle_pos"},
    {SignalIds::kMapKpa, "map_kpa"},
    {SignalIds::kBoostBar, "boost_bar"},
    {SignalIds::kIatC, "iat_c"},
    {SignalIds::kCoolantTempC, "coolant_temp_c"},
    {SignalIds::kOilTempC, "oil_temp_c"},
    {SignalIds::kOilPressBar, "oil_press_bar"},
    {SignalIds::kFuelPressBar, "fuel_press_bar"},
    {SignalIds::kLambda1, "lambda_1"},
    {SignalIds::kAfr1, "afr_1"},
    {SignalIds::kSpeedKph, "speed_kph"},
    {SignalIds::kGear, "gear"},
    {SignalIds::kBatteryVolts, "battery_volts"},
    {SignalIds::kFlagMil, "flag_mil"},
    {SignalIds::kFlagLaunchCtrl, "flag_launch_ctrl"},
    {SignalIds::kMapNumber, "map_number"},
    {SignalIds::kWheelButton1, "wheel_button_1"},
    {SignalIds::kWheelButton2, "wheel_button_2"},
    {SignalIds::kWheelButton3, "wheel_button_3"},
    {SignalIds::kWheelButton4, "wheel_button_4"},
};

char *appendBytes(char *p, const char *end, const char *bytes, size_t len) {
    if (len == 0 || p + len >= end)
        return nullptr;
    memcpy(p, bytes, len);
    return p + len;
}

char *appendTeleValue(char *p, const char *end, const TeleEntry &signal, float val, bool first) {
    if (!first) {
        if (p >= end)
            return nullptr;
        *p++ = ',';
    }
    const int n = snprintf(p, static_cast<size_t>(end - p), "\"%s\":", signal.name);
    if (n <= 0 || p + n >= end)
        return nullptr;
    p += n;
    char numBuf[16];
    const size_t numLen = FloatFormat::formatGeneral(numBuf, sizeof(numBuf), val, 3);
    return appendBytes(p, end, numBuf, numLen);
}

char *appendTeleValues(char *p, const char *end) {
    bool first = true;
    for (const TeleEntry &signal : kTeleSignals) {
        float val = 0.0f;
        if (!SignalStore::tryReadDisplay(signal.id, &val))
            continue;
        char *next = appendTeleValue(p, end, signal, val, first);
        if (next == nullptr)
            return p;
        p = next;
        first = false;
    }
    return p;
}

}

void UsbTelemetry::sendLine() {
    char buf[kTeleBufSize];
    const char *end = buf + kTeleBufSize - 2;
    static constexpr char kPrefix[] = "{\"tele\":1,\"v\":{";
    char *p = appendBytes(buf, end, kPrefix, sizeof(kPrefix) - 1);
    if (p == nullptr)
        return;
    p = appendTeleValues(p, end);
    if (p + 3 > end)
        return;
    memcpy(p, "}}\n", 4);
    UsbComm::sendLine(buf);
}
