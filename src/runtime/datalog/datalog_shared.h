#pragma once

#include "app_config.h"

#if APP_SD_DATALOG_ENABLED

    #include <stddef.h>
    #include <stdint.h>

namespace DatalogShared {

[[nodiscard]] bool reserve();

void enqueue(const uint8_t *record, size_t len);

[[nodiscard]] size_t take(uint8_t *out, size_t capacity);

[[nodiscard]] uint32_t dropped();

void beginRecording(uint32_t startMs);

void endRecording();

[[nodiscard]] bool recording();

[[nodiscard]] uint32_t startMs();

}

#endif
