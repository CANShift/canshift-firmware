#pragma once

#include "config/config_types.h"
#include "ui/sensor_color_ramp.h"
#include "ui/widget_styles.h"
#include "ui/widgets/widget_tag_pool.h"

#include <lvgl.h>
#include <stddef.h>
#include <stdint.h>

namespace WidgetHelpers {

constexpr uint32_t kZoneNormalRgb = 0x00CC44;
constexpr uint32_t kZoneWarningRgb = 0xFF8800;
constexpr uint32_t kZoneDangerRgb = 0xFF4444;

float clampPct(float value, float minValue, float maxValue);

void formatSignalLabel(const char *src, char *out, size_t outLen);

int formatValue(char *out, size_t outLen, const char *prefix, uint8_t decimals, float value,
                const char *suffix);

bool setLabelTextIfChanged(lv_obj_t *label, const char *text);

const CfgColorRamp *resolveSignalRamp(const char *signalId);

const char *resolveDisplayUnit(const char *signalId, const char *configSuffix);

void initContainer(lv_obj_t *cont, const CfgWidget &cfg, int16_t yOffset, bool hasBorder,
                   uint32_t borderRgb);

inline void disableInteract(lv_obj_t *obj) {
    if (!obj)
        return;
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
}

template <typename T>
inline void attachTagDeleter(lv_obj_t *obj, T *tag) {
    if (!obj || !tag)
        return;
    lv_obj_add_event_cb(
        obj,
        [](lv_event_t *e) {
            auto *t = static_cast<T *>(lv_event_get_user_data(e));
            if (!t)
                return;
            auto *target = static_cast<lv_obj_t *>(lv_event_get_current_target(e));
            if (target)
                lv_anim_del(target, nullptr);
            WidgetTagPool::release(t);
        },
        LV_EVENT_DELETE, tag);
}

} // namespace WidgetHelpers
