#pragma once
// widget_helpers.h — Shared widget drawing helpers.
//
// Centralises the small math, formatting, container-boilerplate, and
// LVGL-lifecycle snippets that every widget in src/ui/widgets/ used to
// repeat verbatim. Anything that touched LVGL still requires the caller
// to hold g_lvglMutex, matching the convention used by widget_styles.

#include "config/config_types.h"
#include "ui/sensor_color_ramp.h"
#include "ui/widget_styles.h"
#include "ui/widgets/widget_tag_pool.h"

#include <lvgl.h>
#include <stddef.h>
#include <stdint.h>

namespace WidgetHelpers {

// Fixed automotive zone palette — drivers expect green/orange/red semantics
// regardless of how the widget's brand colour is set in studio.
constexpr uint32_t kZoneNormalRgb = 0x00CC44;  // green
constexpr uint32_t kZoneWarningRgb = 0xFF8800; // orange
constexpr uint32_t kZoneDangerRgb = 0xFF4444;  // red

// Map `value` into [0,1] via (value-min)/(max-min). Returns 0 when the range
// is degenerate (max <= min).
float clampPct(float value, float minValue, float maxValue);

// Convert "coolant_temp_c" → "COOLANT TEMP C". Empty src → "-".
void formatSignalLabel(const char *src, char *out, size_t outLen);

// snprintf wrapper for `<prefix><value formatted with `decimals` dp><suffix>`.
// Suffix may be nullptr (treated as empty). Returns bytes written (snprintf
// semantics).
int formatValue(char *out, size_t outLen, const char *prefix, uint8_t decimals, float value,
                const char *suffix);

// Set `text` on `label` only when it differs from the current contents.
// Returns true when the underlying lv_label_set_text call was issued.
bool setLabelTextIfChanged(lv_obj_t *label, const char *text);

// Resolve the active color ramp for `signalId`: per-signal ramp from
// signals.json wins, sensor-name heuristic next, nullptr → caller falls
// back to the legacy static path. Defined in the .cpp so widget headers
// don't drag in config_loader.h.
const CfgColorRamp *resolveSignalRamp(const char *signalId);

// Resolve the unit string a widget should display next to its value.
// Precedence: explicit widget config suffix (legacy override) > the unit
// declared on the bound signal in signals.json > empty. Returns a stable
// non-null `const char *` that points either into the caller's config
// memory or into the signal definition cache — safe to hold for the life
// of the widget. Defined in the .cpp so widget headers don't drag in
// config_loader.h.
const char *resolveDisplayUnit(const char *signalId, const char *configSuffix);

// Apply the standard widget-container boilerplate: position + size +
// non-scrollable + transparent base style with optional border.
void initContainer(lv_obj_t *cont, const CfgWidget &cfg, int16_t yOffset, bool hasBorder,
                   uint32_t borderRgb);

// Inline helpers ----------------------------------------------------------

// Clear the scrollable + clickable flags on inner overlay objects so they
// never absorb gestures meant for the parent.
inline void disableInteract(lv_obj_t *obj) {
    if (!obj)
        return;
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
}

// Register the canonical LV_EVENT_DELETE callback for a widget whose Tag
// lives in WidgetTagPool. Wipes every animation bound to the object before
// releasing the slot, so any queued exec_cb on the next LVGL tick cannot
// fire against memory the pool may have already handed to a different
// widget (issue #886). Safe to use on widgets that never register
// animations — lv_anim_del is a no-op there. Promoted from the per-widget
// callbacks in button + warning so every site shares one delete path
// (F-LO-5).
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
