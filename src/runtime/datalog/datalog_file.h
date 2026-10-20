#pragma once

#include "app_config.h"

#if APP_SD_DATALOG_ENABLED

    #include <stddef.h>
    #include <stdint.h>

namespace DatalogFile {

enum class Result : uint8_t { Ok, Full, Failed };

[[nodiscard]] Result open(uint16_t &session, uint8_t *scratch, size_t capacity);

[[nodiscard]] Result append(const uint8_t *bytes, size_t len);

[[nodiscard]] Result sync();

[[nodiscard]] Result finish(uint32_t tMs, uint32_t dropped);

void abandon();

}

#endif
