#pragma once

#include <stddef.h>
#include <stdint.h>

namespace DefaultFonts {

struct ProvisionResult {
    uint8_t written;
    uint8_t skipped;
    uint8_t failed;
};

ProvisionResult provisionMissingFiles();

} // namespace DefaultFonts
