#include "rev_limit_flash.h"

#include <atomic>

namespace {

std::atomic<bool> s_engaged{false};
std::atomic<bool> s_rowLit{true};

}

void RevLimitFlash::set(bool limiterEngaged, bool rowLit) {
    s_engaged.store(limiterEngaged, std::memory_order_relaxed);
    s_rowLit.store(!limiterEngaged || rowLit, std::memory_order_relaxed);
}

bool RevLimitFlash::isEngaged() {
    return s_engaged.load(std::memory_order_relaxed);
}

bool RevLimitFlash::isRowLit() {
    return s_rowLit.load(std::memory_order_relaxed);
}
