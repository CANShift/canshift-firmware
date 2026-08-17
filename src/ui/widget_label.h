#pragma once

#include "ui/theme_tokens.h"

#include <stdint.h>

struct _lv_obj_t;

namespace WidgetLabelOverlay {

enum class HeaderPos : uint8_t {
    TOP_LEFT = 0,
    BOTTOM_LEFT = 1,
};

constexpr uint32_t kLabelDimRgb = ThemeTokens::kDimNight;

_lv_obj_t *applySignalHeader(_lv_obj_t *cont, const char *signalId,
                             HeaderPos pos = HeaderPos::TOP_LEFT);

} // namespace WidgetLabelOverlay
