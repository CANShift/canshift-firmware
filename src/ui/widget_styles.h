#pragma once
// widget_styles.h — Shared LVGL style helpers + per-frame write guards.
//
// Centralises the boilerplate that every widget in src/ui/widgets/ used to
// repeat verbatim (transparent container with optional border, square track
// rectangles, translucent zone overlays, knob suppression on arcs). Also
// exposes "set-if-changed" guards that let update() paths skip redundant
// LVGL style writes when the cached colour matches the target.
//
// Threading: callers must hold g_lvglMutex before invoking any helper here,
// matching the convention used by everything that touches LVGL objects.

#include <lvgl.h>
#include <stdint.h>

namespace WidgetStyles {

// Container-level helpers ----------------------------------------------------

// Opaque-transparent background, zero padding, optional 1 px border at
// borderRgb. Mirrors the boilerplate every widget container ran inline.
void applyContainerBase(lv_obj_t *cont, bool hasBorder, uint32_t borderRgb);

// Same as applyContainerBase(false, …) but skips the border config entirely
// (used by image_widget which never draws a border).
void applyContainerBaseNoBorder(lv_obj_t *cont);

// Bar-track helpers ----------------------------------------------------------

// Translucent coloured zone overlay (warning/danger band). Square corners,
// no border, no padding, opacity controlled by caller for layout flexibility.
void applyBarZone(lv_obj_t *zone, uint32_t bgRgb, lv_opa_t opa);

// Bar fill strip — fully opaque, square corners, ready for size/position
// updates each tick.
void applyBarFill(lv_obj_t *fill, uint32_t bgRgb);

// Bar background track — neutral fill, square corners. Caller sets size.
void applyBarTrack(lv_obj_t *track);

// Arc helpers ----------------------------------------------------------------

// Disable the arc knob (transparent bg + zero pad). Both gauge sector arcs
// and value arcs need this — extracted to avoid the two-line repetition.
void disableArcKnob(lv_obj_t *arc);

// Per-frame write guards -----------------------------------------------------
// Each guard returns true when the underlying style write happened (cached
// colour was stale) and false when the cache hit short-circuited the write.
// Init `cachedRgb` to 0xFFFFFFFFu so the first call always paints — the
// alpha bits never escape a 0x00RRGGBB target so the sentinel is unique.

bool setTextColorIfChanged(lv_obj_t *label, uint32_t &cachedRgb, uint32_t targetRgb);
bool setBgColorIfChanged(lv_obj_t *obj, uint32_t &cachedRgb, uint32_t targetRgb);
bool setArcColorIfChanged(lv_obj_t *arc, uint32_t &cachedRgb, uint32_t targetRgb,
                          lv_part_t part);

} // namespace WidgetStyles
