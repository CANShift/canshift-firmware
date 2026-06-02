// gear_widget.cpp — Large gear indicator widget
//
// Renders the current gear as an oversized centered number.
// Special values:
//   0 (or invalid)  → "N" (neutral)
//  -1 (negative)    → "R" (reverse)
//  1–8              → "1"–"8"
//
// Font is auto-selected from the largest that fits the widget height.

#include "gear_widget.h"
#include "ui/font_manager.h"
#include "ui/theme_manager.h"
#include "ui/widget_styles.h"
#include "ui/widgets/widget_helpers.h"
#include "ui/widgets/widget_tag_pool.h"
#include "diag/logger.h"

#include <lvgl.h>
#include <stdio.h>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace {

// Tag promoted from a bare lv_obj_t* pointer so we can cache the last text
// colour and skip the redundant lv_obj_set_style_text_color writes per tick.
struct GearTag {
    lv_obj_t *label;
    uint32_t lastColorRgb; // 0xFFFFFFFFu sentinel forces the first paint.
};

// GearTag storage comes from the shared WidgetTagPool slab (#1031
// F-HI-2 follow-up). See ui/widgets/widget_tag_pool.h.

// Auto signal-name header band — mirrors label_widget's 14 px constant so
// gear and numeric widgets reserve the same vertical strip when no user label
// is set (widget-parity audit §4 / §6 cross-check).
constexpr int16_t kSigHeaderH = 14;

// Same proportional formula as label_widget so a gear and a numeric widget
// at the same size render at identical font sizes. When an auto header is
// drawn the digit band shrinks by `kSigHeaderH` first — matches the Studio
// preview which subtracts the header height before sizing the digit.
//
// TODO(#18): 12..48 px clamp matches label_widget — see the matching comment
// there. When a larger physical panel ships, the upper bound must grow with
// the vertical scale factor and FontManager will need wider Orbitron tiers.
//
// Font tier: Studio always renders Orbitron Black (weight 900). FontManager
// only bakes Black at 32 / 48 px, so cells smaller than that fall through to
// Bold then Medium — a residual flagged in the widget-parity audit.
const lv_font_t *selectFont(int16_t h, int16_t w) {
    const int byHeight = (h * 85) / 100;
    const int byWidth = (w * 72) / 100;
    int s = byHeight < byWidth ? byHeight : byWidth;
    if (s < 12)
        s = 12;
    if (s > 48)
        s = 48;
    const uint8_t size = static_cast<uint8_t>(s);
    if (size >= 32)
        return FontManager::primary(size);
    if (size >= 20)
        return FontManager::secondary(size);
    return FontManager::label(size);
}

} // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

lv_obj_t *GearWidget::create(lv_obj_t *parent, const CfgWidget &cfg, int16_t yOffset) {
    lv_obj_t *cont = lv_obj_create(parent);
    WidgetHelpers::initContainer(cont, cfg, yOffset, cfg.style.hasBorder,
                                 cfg.style.borderColor.rgb);

    const uint32_t textRgb =
        ThemeManager::getEffectiveTextColor(cfg.style.textColor.rgb, cfg.style.respectDayMode);

    // Gear widget never shows a label — the giant digit communicates the
    // signal (user feedback 2026-06-01: "on sait ce qu'il represente").
    // Digit takes the full widget height.
    const int16_t sigHeaderH = 0;
    const int16_t digitBandH = cfg.layout.h;
    (void)kSigHeaderH;

    lv_obj_t *label = lv_label_create(cont);
    // Bias down by half the header so the digit sits at the centre of the
    // FREE space below the header, not the centre of the whole widget.
    lv_obj_align(label, LV_ALIGN_CENTER, 0, static_cast<int16_t>(sigHeaderH / 2));
    // Initial paint is the neutral "N" — keep the theme text colour here;
    // running state switches to `cfg.style.primaryColor` in update() to match
    // Studio (widget-parity audit §4).
    lv_obj_set_style_text_color(label, lv_color_hex(textRgb), 0);
    lv_obj_set_style_text_font(label, selectFont(digitBandH, cfg.layout.w), 0);
    lv_label_set_text(label, "N");

    GearTag *tag = WidgetTagPool::alloc<GearTag>();
    if (!tag) {
        LOG_WARN("GEAR", "Tag pool exhausted for '%s' (all %u slots busy)", cfg.id,
                 static_cast<unsigned>(WidgetTagPool::kPoolSlots));
        lv_obj_del(cont);
        return nullptr;
    }
    tag->label = label;
    tag->lastColorRgb = textRgb; // First paint above used textRgb (neutral "N" path).
    lv_obj_set_user_data(cont, tag);
    lv_obj_add_event_cb(cont, WidgetTagPool::deleteHandler<GearTag>, LV_EVENT_DELETE, tag);

    // Custom widget label intentionally skipped — see comment at the top of
    // create() for the rationale.

    return cont;
}

void GearWidget::reapplyTheme(lv_obj_t *obj, const CfgWidget &cfg) {
    if (!obj)
        return;
    auto *tag = static_cast<GearTag *>(lv_obj_get_user_data(obj));
    if (!tag || !tag->label)
        return;
    // Re-apply only the neutral-state colour: the running state honours
    // `cfg.style.primaryColor` which doesn't depend on day/night. Update()
    // will overwrite this on the next tick if the live signal is non-zero —
    // safe either way because `setTextColorIfChanged` is idempotent.
    const uint32_t textRgb =
        ThemeManager::getEffectiveTextColor(cfg.style.textColor.rgb, cfg.style.respectDayMode);
    WidgetStyles::setTextColorIfChanged(tag->label, tag->lastColorRgb, textRgb);
}

void GearWidget::update(lv_obj_t *obj, float value, bool valid, const CfgWidget &cfg) {
    if (!obj)
        return;
    auto *tag = static_cast<GearTag *>(lv_obj_get_user_data(obj));
    if (!tag || !tag->label)
        return;
    lv_obj_t *label = tag->label;

    const uint32_t textRgb =
        ThemeManager::getEffectiveTextColor(cfg.style.textColor.rgb, cfg.style.respectDayMode);

    if (!valid || value == 0.0f) {
        // Neutral / no-value path keeps the theme-effective text colour so
        // the "N" placeholder reads against day/night backgrounds.
        WidgetHelpers::setLabelTextIfChanged(label, "N");
        WidgetStyles::setTextColorIfChanged(label, tag->lastColorRgb, textRgb);
        return;
    }

    int32_t gear = static_cast<int32_t>(value);
    if (gear < 0) {
        WidgetHelpers::setLabelTextIfChanged(label, "R");
        WidgetStyles::setTextColorIfChanged(label, tag->lastColorRgb, cfg.style.warningColor.rgb);
        return;
    }

    char buf[4];
    snprintf(buf, sizeof(buf), "%d", gear);
    WidgetHelpers::setLabelTextIfChanged(label, buf);
    // Running state honours the per-widget `primaryColor` (audit §4).
    WidgetStyles::setTextColorIfChanged(label, tag->lastColorRgb, cfg.style.primaryColor.rgb);
}
