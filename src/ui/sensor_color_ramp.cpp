#include "sensor_color_ramp.h"
#include "sensor_color_ramp_rs.h"

SensorKind sensorKindFromName(const char *signalName) {
    return static_cast<SensorKind>(sensor_kind_from_name_rs(signalName));
}

const CfgColorRamp *resolveRamp(const CfgColorRamp &perSignal, const char *signalName) {
    return resolve_ramp_rs(&perSignal, signalName);
}

uint32_t colorAtValue(const CfgColorRamp &ramp, float value) {
    return color_at_value_rs(&ramp, value);
}
