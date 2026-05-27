#pragma once
// widget_label.h — Optional corner label drawn on every widget that supports it.
//
// Mirrors the studio preview's svgLabelAttrs() positioning: a small dim caps
// header pinned to one of the six corners of the widget container. The colour
// is a fixed neutral grey (#888888) — the previous half-luminance trick turned
// medium-saturated text colours (cyan, orange) into unreadable mud against
// the dark dashboard background.

#include "config/config_types.h"

#include <stdint.h>

struct _lv_obj_t;

namespace WidgetLabelOverlay {

// Studio uses #888888 with letter-spacing 0.06em + 5–7 px font. We mirror that
// with Orbitron Medium 12 (smallest compiled-in size that stays legible on
// the 320×240 panel) and a 1-px letter spacing for the same caps look.
constexpr uint32_t kLabelDimRgb = 0x888888;

// No-op when `text` is empty or null. The label is parented to `cont` and
// positioned according to `pos`. The `textColor` arg is kept for ABI stability
// across call sites but is no longer used — labels are always neutral grey so
// they never compete with coloured value text.
void apply(_lv_obj_t *cont, const char *text, CfgLabelPos pos, uint32_t textColor);

// Auto signal-name header — drawn at top-left in the same dim caps style when
// the widget has no user-configured `cfg.label`. Uses the curated dictionary
// when available, otherwise falls back to a simple uppercase / underscore-to-
// space transform of the signal id.
void applySignalHeader(_lv_obj_t *cont, const char *signalId);

// Curated short labels for known signals — fits the 80-px-wide cells used by
// the small numeric widgets where the auto-formatted "COOLANT TEMP C" would
// overflow. Returns nullptr when the signal isn't in the dictionary; callers
// then fall back to the auto-formatted version.
//
// Keep in sync with the studio dictionary in
// `canshift-studio-web/src/utils/signalLabels.ts` (if/when it's lifted from
// the now-decommissioned Electron Studio package).
const char *displayLabelForSignal(const char *signalId);

} // namespace WidgetLabelOverlay
