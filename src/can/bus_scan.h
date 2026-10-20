#pragma once

#include "bus_scan_rs.h"

#include <stdint.h>

namespace BusScan {

enum class Tick : uint8_t { Idle, Frame, DriverLost };

[[nodiscard]] bool requestStart();

[[nodiscard]] bool claimsBus();

[[nodiscard]] Tick tick();

void copyReport(BusScanRs &out);

}
