#pragma once

#include "bus_scan_rs.h"

#include <stddef.h>

namespace BusScanLine {

inline constexpr size_t kLineCap = 3456;

[[nodiscard]] size_t format(const BusScanRs &scan, char *out, size_t cap);

}
