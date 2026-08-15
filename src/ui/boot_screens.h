#pragma once

#include <lvgl.h>
#include <stdint.h>

namespace BootScreens {

constexpr uint8_t kCheckCount = 5;

struct CheckResult {
    const char *result;
    bool passed;
};

void buildBoot(lv_obj_t *parent);

void buildSelfTest(lv_obj_t *parent, const CheckResult (&results)[kCheckCount]);

} // namespace BootScreens
