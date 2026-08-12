#pragma once

namespace RevLimitFlash {

void set(bool limiterCritical, bool phaseOn);

[[nodiscard]] bool isBlanked();

} // namespace RevLimitFlash
