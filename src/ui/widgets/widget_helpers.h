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

#include <lvgl.h>
#include <stddef.h>
#include <stdint.h>

namespace WidgetHelpers {

// Fixed automotive zone palette — drivers expect green/orange/red semantics
// regardless of how the widget's brand colour is set in studio.
constexpr uint32_t kZoneNormalRgb = 0x00CC44;  // green
constexpr uint32_t kZoneWarningRgb = 0xFF8800; // orange
constexpr uint32_t kZoneDangerRgb = 0xFF4444;  // red

struct ThresholdZones {
    bool hasWarn;
    bool hasDanger;
    float warnPct;
    float dangerPct;
};

// Map `value` into [0,1] via (value-min)/(max-min). Returns 0 when the range
// is degenerate (max <= min).
float clampPct(float value, float minValue, float maxValue);

// Resolve warning/danger band positions in pct domain. The bar widget feeds
// these straight into the track; the gauge maps pct → arc angle locally.
ThresholdZones resolveZones(float warningLevel, float dangerLevel, float minValue, float maxValue);

// Map a [0,1] pct + warn/danger pcts to the green/orange/red zone fill colour.
uint32_t zoneFillColor(float pct, float warnPct, float dangerPct);

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

// Attach a heap-allocated tag to `obj` and register an LV_EVENT_DELETE
// handler that `delete`s it when the widget is destroyed. Mirrors the
// boilerplate every widget create() ran inline.
template <typename TagT>
void attachTagDeleter(lv_obj_t *obj, TagT *tag) {
    lv_obj_set_user_data(obj, tag);
    lv_obj_add_event_cb(
        obj,
        [](lv_event_t *e) {
            auto *t = static_cast<TagT *>(lv_event_get_user_data(e));
            delete t;
        },
        LV_EVENT_DELETE, tag);
}

} // namespace WidgetHelpers
