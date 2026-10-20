#include "hal/power/cpu_clock.h"

#include "diag/error_store.h"
#include "diag/logger.h"

#include <Arduino.h>

namespace {

constexpr uint32_t kLowPowerCpuMhz = 80;

uint32_t s_nominalCpuMhz = 0;

void switchTo(uint32_t mhz) {
    if (setCpuFrequencyMhz(mhz)) {
        LOG_INFO("POWER", "CPU clock %u MHz", static_cast<unsigned>(mhz));
        return;
    }
    LOG_ERROR("POWER", "CPU clock switch to %u MHz refused", static_cast<unsigned>(mhz));
    ErrorStore::push(ERROR_SRC_SYSTEM, "cpu_clock", "CPU clock switch refused");
}

}

namespace CpuClock {

void enterLowPower() {
    if (s_nominalCpuMhz == 0)
        s_nominalCpuMhz = getCpuFrequencyMhz();
    switchTo(kLowPowerCpuMhz);
}

void leaveLowPower() {
    if (s_nominalCpuMhz == 0)
        return;
    switchTo(s_nominalCpuMhz);
}

}
