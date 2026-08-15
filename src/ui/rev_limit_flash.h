#pragma once

namespace RevLimitFlash {

void set(bool limiterEngaged, bool rowLit);

[[nodiscard]] bool isEngaged();

[[nodiscard]] bool isRowLit();

} // namespace RevLimitFlash
