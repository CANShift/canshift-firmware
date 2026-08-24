#include "widget_helpers.h"

#include "alert_engine_rs.h"

#include "config/config_loader.h"
#include "diag/error_store.h"
#include "ui/font_manager.h"
#include "diag/logger.h"
#include "ui/screen_profile.h"
#include "ui/unit_display.h"
#include "ui/widget_label.h"
#include "util/format_float.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

namespace WidgetHelpers {

ScaledBox scaledBox(const CfgWidget &cfg) {
    return {ScreenProfile::scaleXVal(cfg.layout.w), ScreenProfile::scaleYVal(cfg.layout.h)};
}

int16_t scaledSquare(const CfgWidget &cfg) {
    const ScaledBox box = scaledBox(cfg);
    return box.w < box.h ? box.w : box.h;
}

float clampPct(float value, float minValue, float maxValue) {
    if (maxValue <= minValue)
        return 0.0f;
    float pct = (value - minValue) / (maxValue - minValue);
    if (pct < 0.0f)
        return 0.0f;
    if (pct > 1.0f)
        return 1.0f;
    return pct;
}

void formatStalePlaceholder(char *out, size_t outLen, float maxValue) {
    if (!out || outLen == 0)
        return;
    const uint8_t groups = alert_stale_dash_groups_rs(maxValue);
    size_t used = 0;
    for (uint8_t i = 0; i < groups && used + 2 < outLen; ++i) {
        if (i > 0)
            out[used++] = ' ';
        out[used++] = '-';
    }
    out[used] = '\0';
}

void formatSignalLabel(const char *src, char *out, size_t outLen) {
    if (outLen == 0)
        return;
    if (!src || src[0] == '\0') {
        strlcpy(out, "-", outLen);
        return;
    }
    size_t j = 0;
    for (size_t i = 0; src[i] != '\0' && j + 1 < outLen; ++i) {
        char c = src[i];
        if (c == '_')
            c = ' ';
        out[j++] = static_cast<char>(toupper(static_cast<unsigned char>(c)));
    }
    out[j] = '\0';
}

int formatValue(char *out, size_t outLen, const char *prefix, uint8_t decimals, float value,
                const char *suffix) {
    if (!out || outLen == 0)
        return 0;
    out[0] = '\0';

    const char *p = prefix ? prefix : "";
    const char *s = suffix ? suffix : "";

    size_t pos = 0;
    const size_t prefixLen = strlen(p);
    const size_t copyPrefix = (prefixLen < outLen - 1) ? prefixLen : outLen - 1;
    memcpy(out + pos, p, copyPrefix);
    pos += copyPrefix;
    out[pos] = '\0';

    if (pos + 1 < outLen) {
        FloatFormat::formatFixed(out + pos, outLen - pos, value, static_cast<int>(decimals));
        pos += strlen(out + pos);
    }

    if (pos + 1 < outLen) {
        const size_t suffixLen = strlen(s);
        const size_t copySuffix = (suffixLen < outLen - 1 - pos) ? suffixLen : outLen - 1 - pos;
        memcpy(out + pos, s, copySuffix);
        pos += copySuffix;
        out[pos] = '\0';
    }
    return static_cast<int>(pos);
}

bool setLabelTextIfChanged(lv_obj_t *label, const char *text) {
    if (!label || !text)
        return false;
    const char *current = lv_label_get_text(label);
    if (current != nullptr && strcmp(current, text) == 0)
        return false;
    lv_label_set_text(label, text);
    return true;
}

float resolveWarnLevel(const char *signalId, float dangerLevel, bool dangerBelow) {
    if (!signalId || signalId[0] == '\0')
        return NAN;
    const CfgSignalDef *def = ConfigLoader::findSignal(signalId);
    if (!def)
        return NAN;
    return alert_warn_level_for_rs(dangerLevel, dangerBelow, def->warningLevel,
                                   def->highWarningLevel);
}

void initContainer(lv_obj_t *cont, const CfgWidget &cfg, int16_t yOffset, bool hasBorder,
                   uint32_t borderRgb) {
    if (!cont)
        return;

    const int16_t px = ScreenProfile::scaleXVal(cfg.layout.x);
    const int16_t py = static_cast<int16_t>(ScreenProfile::scaleYVal(cfg.layout.y) + yOffset);
    lv_obj_set_pos(cont, px, py);
    lv_obj_set_size(cont, ScreenProfile::scaleXVal(cfg.layout.w),
                    ScreenProfile::scaleYVal(cfg.layout.h));
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
    WidgetStyles::applyContainerBase(cont, hasBorder, borderRgb);
}

void resetContainerStyle(lv_obj_t *obj) {
    if (!obj)
        return;
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(obj, 0, LV_PART_MAIN);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
}

lv_obj_t *makeSquareBadge(lv_obj_t *parent, int16_t side, uint32_t rgb) {
    lv_obj_t *badge = lv_obj_create(parent);
    if (!badge)
        return nullptr;
    lv_obj_set_size(badge, side, side);
    lv_obj_set_style_radius(badge, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(badge, lv_color_hex(rgb), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(badge, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(badge, 0, LV_PART_MAIN);
    lv_obj_clear_flag(badge, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return badge;
}

lv_obj_t *makeTopRule(lv_obj_t *cont, uint8_t heightPx, uint32_t rgb) {
    lv_obj_t *rule = lv_obj_create(cont);
    if (!rule)
        return nullptr;
    lv_obj_set_size(rule, LV_PCT(100), heightPx);
    lv_obj_align(rule, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(rule, lv_color_hex(rgb), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(rule, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(rule, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(rule, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(rule, 0, LV_PART_MAIN);
    lv_obj_clear_flag(rule, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return rule;
}

lv_obj_t *makeFlushColumn(lv_obj_t *parent) {
    lv_obj_t *col = lv_obj_create(parent);
    if (!col)
        return nullptr;
    resetContainerStyle(col);
    lv_obj_set_size(col, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(col, 0, LV_PART_MAIN);
    return col;
}

namespace {

int16_t textWidthPx(const char *text, const lv_font_t *font, int16_t trackingPx) {
    lv_point_t size = {};
    lv_txt_get_size(&size, text, font, trackingPx, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    return static_cast<int16_t>(size.x);
}

float widestConfiguredValue(const CfgWidget &cfg) {
    const float maxValue = cfg.type == WidgetType::GAUGE ? cfg.gauge.maxValue : cfg.label.maxValue;
    const float minValue = cfg.type == WidgetType::GAUGE ? cfg.gauge.minValue : cfg.label.minValue;
    return -minValue > maxValue ? minValue : maxValue;
}

const char *configuredSuffix(const CfgWidget &cfg) {
    return cfg.type == WidgetType::GAUGE ? cfg.gauge.suffix : cfg.label.suffix;
}

} // namespace

void reportValueOverflow(const CfgWidget &cfg, const lv_font_t *font, int16_t trackingPx) {
    if (!font || cfg.layout.w <= 0)
        return;
    const uint8_t decimals =
        cfg.type == WidgetType::GAUGE ? cfg.gauge.decimalPlaces : cfg.label.decimalPlaces;
    const char *prefix = cfg.type == WidgetType::GAUGE ? cfg.gauge.prefix : cfg.label.prefix;
    const char *suffix = configuredSuffix(cfg);
    const char *unit = UnitDisplay::displayUnitFor(cfg.signalId, suffix);
    char widest[40];
    formatValue(widest, sizeof(widest), prefix, decimals,
                UnitDisplay::valueFor(widestConfiguredValue(cfg),
                                      UnitDisplay::convertibleUnitFor(cfg.signalId, suffix)),
                nullptr);
    int16_t needed = textWidthPx(widest, font, trackingPx);
    if (unit && unit[0] != '\0')
        needed = static_cast<int16_t>(needed + textWidthPx(unit, FontManager::units(), 0));
    const int16_t available = static_cast<int16_t>(scaledBox(cfg).w - kValueRightInsetPx);
    if (needed <= available)
        return;
    LOG_ERROR("WF", "Widget '%s': '%s%s' needs %d px, column gives %d — layout does not fit",
              cfg.id, widest, unit ? unit : "", static_cast<int>(needed),
              static_cast<int>(available));
    ErrorStore::push(ERROR_SRC_CONFIG, "OVERFLOW", cfg.id);
}

void animateFill(lv_obj_t *obj, lv_anim_exec_xcb_t setter, int32_t from, int32_t to) {
    if (!obj)
        return;
    lv_anim_del(obj, setter);
    if (from == to) {
        setter(obj, to);
        return;
    }
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, obj);
    lv_anim_set_exec_cb(&a, setter);
    lv_anim_set_values(&a, from, to);
    lv_anim_set_time(&a, kFillCatchUpMs);
    lv_anim_set_path_cb(&a, lv_anim_path_linear);
    lv_anim_start(&a);
}
void setFillImmediate(lv_obj_t *obj, lv_anim_exec_xcb_t setter, int32_t value) {
    if (!obj)
        return;
    lv_anim_del(obj, setter);
    setter(obj, value);
}

void logTagPoolExhausted(const char *logTag, const char *widgetId) {
    LOG_WARN(logTag, "Tag pool exhausted for '%s' (all %u slots busy)", widgetId,
             static_cast<unsigned>(WidgetTagPool::kPoolSlots));
}

} // namespace WidgetHelpers
