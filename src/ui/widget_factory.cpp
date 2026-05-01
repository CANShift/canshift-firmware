// widget_factory.cpp — Widget instantiation and update dispatch

#include "widget_factory.h"
#include "widgets/gauge_widget.h"
#include "widgets/bar_widget.h"
#include "widgets/label_widget.h"
#include "widgets/warning_widget.h"
#include "widgets/button_widget.h"
#include "widgets/gear_widget.h"
#include "widgets/timer_widget.h"
#include "widgets/image_widget.h"
#include "runtime/signal_store.h"
#include "can/signal_map.h"
#include "diag/logger.h"

#include <lvgl.h>
#include <stdint.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Widget registry
// ---------------------------------------------------------------------------

namespace {

// Maximum widgets tracked across all pages (adjust if needed)
static constexpr uint8_t MAX_TRACKED_WIDGETS = CONFIG_MAX_PAGES * CONFIG_MAX_WIDGETS_PER_PAGE;

struct WidgetEntry {
    lv_obj_t *parent; // Owning page screen
    lv_obj_t *obj;    // LVGL object
    WidgetType type;
    char signalId[CFG_MAX_SIGNAL_LEN];
    CfgWidget cfg; // Copy of config for update logic
    bool active;
};

static WidgetEntry s_widgets[MAX_TRACKED_WIDGETS];
static uint8_t s_widgetCount = 0;

SignalId resolveSignalId(const char *name) {
    // Map signal name string to SignalId constant
    // This is a simple linear lookup. For large signal sets, use a hash map.
    // Signal name strings must match signal_map.h constants.
    if (strcmp(name, "rpm") == 0)
        return SignalIds::RPM;
    if (strcmp(name, "throttle_pos") == 0)
        return SignalIds::THROTTLE_POS;
    if (strcmp(name, "map_kpa") == 0)
        return SignalIds::MAP_KPA;
    if (strcmp(name, "boost_bar") == 0)
        return SignalIds::BOOST_BAR;
    if (strcmp(name, "iat_c") == 0)
        return SignalIds::IAT_C;
    if (strcmp(name, "coolant_temp_c") == 0)
        return SignalIds::COOLANT_TEMP_C;
    if (strcmp(name, "oil_temp_c") == 0)
        return SignalIds::OIL_TEMP_C;
    if (strcmp(name, "oil_press_bar") == 0)
        return SignalIds::OIL_PRESS_BAR;
    if (strcmp(name, "fuel_press_bar") == 0)
        return SignalIds::FUEL_PRESS_BAR;
    if (strcmp(name, "lambda_1") == 0)
        return SignalIds::LAMBDA_1;
    if (strcmp(name, "afr_1") == 0)
        return SignalIds::AFR_1;
    if (strcmp(name, "speed_kph") == 0)
        return SignalIds::SPEED_KPH;
    if (strcmp(name, "gear") == 0)
        return SignalIds::GEAR;
    if (strcmp(name, "battery_volts") == 0)
        return SignalIds::BATTERY_VOLTS;
    if (strcmp(name, "flag_mil") == 0)
        return SignalIds::FLAG_MIL;
    if (strcmp(name, "map_number") == 0)
        return SignalIds::MAP_NUMBER;
    LOG_WARN("WF", "Unknown signal name: %s", name);
    return SignalIds::SIGNAL_COUNT; // Invalid
}

lv_obj_t *createGauge(lv_obj_t *parent, const CfgWidget &cfg, int16_t yOffset) {
    lv_obj_t *obj = GaugeWidget::create(parent, cfg, yOffset);
    return obj;
}

lv_obj_t *createLabel(lv_obj_t *parent, const CfgWidget &cfg, int16_t yOffset) {
    lv_obj_t *obj = LabelWidget::create(parent, cfg, yOffset);
    return obj;
}

lv_obj_t *createWarning(lv_obj_t *parent, const CfgWidget &cfg, int16_t yOffset) {
    lv_obj_t *obj = WarningWidget::create(parent, cfg, yOffset);
    return obj;
}

lv_obj_t *createButton(lv_obj_t *parent, const CfgWidget &cfg, int16_t yOffset) {
    lv_obj_t *obj = ButtonWidget::create(parent, cfg, yOffset);
    return obj;
}

lv_obj_t *createBar(lv_obj_t *parent, const CfgWidget &cfg, int16_t yOffset) {
    lv_obj_t *obj = BarWidget::create(parent, cfg, yOffset);
    return obj;
}

lv_obj_t *createGear(lv_obj_t *parent, const CfgWidget &cfg, int16_t yOffset) {
    return GearWidget::create(parent, cfg, yOffset);
}

lv_obj_t *createTimer(lv_obj_t *parent, const CfgWidget &cfg, int16_t yOffset) {
    return TimerWidget::create(parent, cfg, yOffset);
}

lv_obj_t *createImage(lv_obj_t *parent, const CfgWidget &cfg, int16_t yOffset) {
    return ImageWidget::create(parent, cfg, yOffset);
}

void updateWidget(WidgetEntry &entry) {
    SignalId sid = resolveSignalId(entry.signalId);
    if (sid >= SignalIds::SIGNAL_COUNT)
        return;

    bool valid = SignalStore::isValid(sid);
    float value = SignalStore::read(sid, 0.0f);
    float rawValue = SignalStore::readRaw(sid, 0.0f);

    switch (entry.type) {
        case WidgetType::GAUGE:
            GaugeWidget::update(entry.obj, value, valid, entry.cfg);
            break;
        case WidgetType::BAR:
            BarWidget::update(entry.obj, value, valid, entry.cfg);
            break;
        case WidgetType::LABEL:
            LabelWidget::update(entry.obj, rawValue, valid, entry.cfg);
            break;
        case WidgetType::WARNING:
            WarningWidget::update(entry.obj, rawValue, valid, entry.cfg);
            break;
        case WidgetType::BUTTON:
            // Buttons respond to touch events — no signal-driven update needed
            break;
        case WidgetType::GEAR_IND:
            GearWidget::update(entry.obj, rawValue, valid, entry.cfg);
            break;
        case WidgetType::TIMER:
            TimerWidget::update(entry.obj, value, valid, entry.cfg);
            break;
        case WidgetType::IMAGE:
            // Static image — no per-frame update needed
            break;
        default:
            break;
    }
}

} // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

lv_obj_t *WidgetFactory::create(lv_obj_t *parent, const CfgWidget &cfg, int16_t yOffset) {
    if (s_widgetCount >= MAX_TRACKED_WIDGETS) {
        LOG_ERROR("WF", "Widget registry full — cannot create widget '%s'", cfg.id);
        return nullptr;
    }

    lv_obj_t *obj = nullptr;

    switch (cfg.type) {
        case WidgetType::GAUGE:
            obj = createGauge(parent, cfg, yOffset);
            break;
        case WidgetType::BAR:
            obj = createBar(parent, cfg, yOffset);
            break;
        case WidgetType::LABEL:
            obj = createLabel(parent, cfg, yOffset);
            break;
        case WidgetType::GEAR_IND:
            obj = createGear(parent, cfg, yOffset);
            break;
        case WidgetType::TIMER:
            obj = createTimer(parent, cfg, yOffset);
            break;
        case WidgetType::IMAGE:
            obj = createImage(parent, cfg, yOffset);
            break;
        case WidgetType::WARNING:
            obj = createWarning(parent, cfg, yOffset);
            break;
        case WidgetType::BUTTON:
            obj = createButton(parent, cfg, yOffset);
            break;
        default:
            LOG_WARN("WF", "Unknown widget type for '%s'", cfg.id);
            return nullptr;
    }

    if (!obj) {
        LOG_ERROR("WF", "Failed to create widget '%s'", cfg.id);
        return nullptr;
    }

    // Register in tracking table
    WidgetEntry &entry = s_widgets[s_widgetCount++];
    entry.parent = parent;
    entry.obj = obj;
    entry.type = cfg.type;
    entry.cfg = cfg;
    entry.active = true;
    strlcpy(entry.signalId, cfg.signalId, CFG_MAX_SIGNAL_LEN);

    LOG_DEBUG("WF", "Created widget '%s' type=%d at (%d,%d)", cfg.id, cfg.type, cfg.layout.x,
              cfg.layout.y + yOffset);

    return obj;
}

void WidgetFactory::updateAll(lv_obj_t *parent) {
    for (uint8_t i = 0; i < s_widgetCount; ++i) {
        if (s_widgets[i].active && s_widgets[i].parent == parent) {
            updateWidget(s_widgets[i]);
        }
    }
}

void WidgetFactory::clearAll(lv_obj_t *parent) {
    for (uint8_t i = 0; i < s_widgetCount; ++i) {
        if (s_widgets[i].parent == parent) {
            s_widgets[i].active = false;
        }
    }
}
