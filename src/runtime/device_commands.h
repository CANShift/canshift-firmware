#pragma once

#include <ArduinoJson.h>
#include <stddef.h>
#include <stdint.h>

namespace DeviceCommands {

enum class Outcome : uint8_t {
    Ok,
    MissingDayField,
    RebootRequested,
};

inline constexpr uint8_t kNoUsbCode = 0;

struct Command {
    uint8_t usbCode;
    const char *bleName;
    Outcome (*run)(const JsonObjectConst &);
};

const Command *findByUsbCode(uint8_t code);
const Command *findByBleName(const char *name);

} // namespace DeviceCommands
