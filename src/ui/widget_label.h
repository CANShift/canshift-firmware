#pragma once

#include "ui/theme_tokens.h"

#include <stddef.h>
#include <stdint.h>

struct _lv_obj_t;

namespace WidgetLabelOverlay {

enum class HeaderPos : uint8_t {
    TOP_LEFT = 0,
    BOTTOM_LEFT = 1,
};

constexpr uint32_t kLabelDimRgb = ThemeTokens::kDimNight;

constexpr size_t kKickerBufLen = 32;

void formatKicker(const char *raw, char *out, size_t outLen);

void signalKicker(const char *signalId, char *out, size_t outLen);

_lv_obj_t *applySignalHeader(_lv_obj_t *cont, const char *signalId,
                             HeaderPos pos = HeaderPos::TOP_LEFT);

_lv_obj_t *applyWidgetHeader(_lv_obj_t *cont, const char *kicker, const char *signalId,
                             HeaderPos pos = HeaderPos::TOP_LEFT);

}
