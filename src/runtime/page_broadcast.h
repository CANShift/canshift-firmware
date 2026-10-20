#pragma once

#include "app_config.h"
#include "config/config_types.h"

#include <ArduinoJson.h>
#include <stddef.h>
#include <stdint.h>

namespace PageBroadcast {

constexpr uint8_t kNoCurrentPage = 0xFF;

struct Snapshot {
    char ids[kConfigMaxPages][kCfgMaxIdLen];
    uint8_t count;
    uint8_t current;
};

void publish(const Snapshot &snapshot);

[[nodiscard]] Snapshot latest();

[[nodiscard]] const char *currentId(const Snapshot &snapshot);

void writePageList(JsonObject out, const Snapshot &snapshot);

}
