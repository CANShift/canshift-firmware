#include "rev_limit_flash.h"

namespace {

bool s_blanked = false;

} // namespace

void RevLimitFlash::set(bool limiterCritical, bool phaseOn) {
    s_blanked = limiterCritical && !phaseOn;
}

bool RevLimitFlash::isBlanked() {
    return s_blanked;
}
