#pragma once

#include <stdint.h>

struct _lv_obj_t;

namespace WidgetLabelOverlay {

enum class HeaderPos : uint8_t {
    TOP_LEFT = 0,
    BOTTOM_LEFT = 1,
};

constexpr uint32_t kLabelDimRgb = 0x888888;

void applySignalHeader(_lv_obj_t *cont, const char *signalId, HeaderPos pos = HeaderPos::TOP_LEFT);

const char *displayLabelForSignal(const char *signalId);

} // namespace WidgetLabelOverlay
