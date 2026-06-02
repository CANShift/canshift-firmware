#pragma once
// widget_label.h — Auto signal-name header drawn at the top of widgets that
// need a caption. Custom per-widget labels were removed in issue #1244 — the
// signal name in dim uppercase is the only label path now.
//
// Colour is a fixed neutral grey (#888888) — the previous half-luminance trick
// turned medium-saturated text colours (cyan, orange) into unreadable mud
// against the dark dashboard background.

#include <stdint.h>

struct _lv_obj_t;

namespace WidgetLabelOverlay {

// Where the auto signal-name header sits on the widget.
enum class HeaderPos : uint8_t {
    TOP_LEFT = 0,
    BOTTOM_LEFT = 1,
};

// Studio uses #888888 with letter-spacing 0.06em + 5–7 px font. We mirror that
// with Orbitron Medium 12 (smallest compiled-in size that stays legible on
// the 320×240 panel) and a 1-px letter spacing for the same caps look.
constexpr uint32_t kLabelDimRgb = 0x888888;

// Auto signal-name header — dim caps drawn at the requested corner. Uses the
// curated dictionary when available, otherwise falls back to a simple
// uppercase / underscore-to-space transform of the signal id. No-op when
// `signalId` is empty or null. `pos` defaults to TOP_LEFT to keep the
// existing numeric-label band on numeric / timer / gear widgets; arc gauges
// pass BOTTOM_LEFT so the header sits clear of the centred value text.
void applySignalHeader(_lv_obj_t *cont, const char *signalId, HeaderPos pos = HeaderPos::TOP_LEFT);

// Curated short labels for known signals — fits the 80-px-wide cells used by
// the small numeric widgets where the auto-formatted "COOLANT TEMP C" would
// overflow. Returns nullptr when the signal isn't in the dictionary; callers
// then fall back to the auto-formatted version.
//
// Keep in sync with the studio dictionary in
// `canshift-studio-web/src/utils/signalLabels.ts`.
const char *displayLabelForSignal(const char *signalId);

} // namespace WidgetLabelOverlay
