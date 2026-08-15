#include "rev_limit_flash.h"

namespace {

bool s_engaged = false;
bool s_rowLit = true;

} // namespace

void RevLimitFlash::set(bool limiterEngaged, bool rowLit) {
    s_engaged = limiterEngaged;
    s_rowLit = !limiterEngaged || rowLit;
}

bool RevLimitFlash::isEngaged() {
    return s_engaged;
}

bool RevLimitFlash::isRowLit() {
    return s_rowLit;
}
