#pragma once

#include <ArduinoJson.h>
#include <stdint.h>

namespace SoundSettingsWire {

struct Values {
    uint8_t volumePct;
    bool boot;
    bool shiftTone;
};

inline constexpr Values kDefaults = {70, true, false};

enum class Merge : uint8_t { Absent, Changed, Invalid };

[[nodiscard]] Merge merge(JsonObjectConst settings, Values &values);

void write(JsonObject out, const Values &values);

}
