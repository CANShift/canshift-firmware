#pragma once

#include "app_config.h"
#include "runtime/sound_settings_wire.h"

#include <ArduinoJson.h>

namespace SoundSettings {

#if APP_AUDIO_ENABLED

void load();

[[nodiscard]] bool available();

[[nodiscard]] SoundSettingsWire::Values current();

[[nodiscard]] bool apply(JsonObjectConst settings);

void writeTo(JsonObject out);

#else

inline void load() {}

inline bool available() {
    return false;
}

inline bool apply(JsonObjectConst) {
    return true;
}

inline void writeTo(JsonObject) {}

#endif

}
