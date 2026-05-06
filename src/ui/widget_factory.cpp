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
    SignalId signalId; // Resolved once at create time — SIGNAL_COUNT means none
    CfgWidget cfg;     // Copy of config for update logic
};

static WidgetEntry s_widgets[MAX_TRACKED_WIDGETS];
static uint8_t s_widgetCount = 0;

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
    if (entry.signalId >= SignalIds::SIGNAL_COUNT)
        return;

    bool valid = SignalStore::isValid(entry.signalId);
    float value = SignalStore::read(entry.signalId, 0.0f);
    float rawValue = SignalStore::readRaw(entry.signalId, 0.0f);

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

    // Resolve the signal name once here so updateAll() (called per render
    // tick) does no string work. Empty signalId is legal — button/image
    // widgets carry none, and signalIdFromName returns SIGNAL_COUNT for them
    // without warning. Warn only for genuinely unknown non-empty names.
    SignalId resolved = signalIdFromName(cfg.signalId);
    if (resolved >= SignalIds::SIGNAL_COUNT && cfg.signalId[0] != '\0') {
        LOG_WARN("WF", "Unknown signal name: %s", cfg.signalId);
    }

    // Register in tracking table
    WidgetEntry &entry = s_widgets[s_widgetCount++];
    entry.parent = parent;
    entry.obj = obj;
    entry.type = cfg.type;
    entry.cfg = cfg;
    entry.signalId = resolved;

    LOG_DEBUG("WF", "Created widget '%s' type=%d at (%d,%d)", cfg.id, cfg.type, cfg.layout.x,
              cfg.layout.y + yOffset);

    return obj;
}

void WidgetFactory::updateAll(lv_obj_t *parent) {
    for (uint8_t i = 0; i < s_widgetCount; ++i) {
        if (s_widgets[i].parent == parent) {
            updateWidget(s_widgets[i]);
        }
    }
}

// Drop every entry whose parent matches `parent` and compact the array in place.
// Called from PageManager::rebuildAllPages() before each page screen is deleted —
// without this, the registry grows unbounded across theme toggles and eventually
// hits MAX_TRACKED_WIDGETS, silently refusing to create further widgets (#57).
void WidgetFactory::clearAll(lv_obj_t *parent) {
    uint8_t out = 0;
    for (uint8_t in = 0; in < s_widgetCount; ++in) {
        if (s_widgets[in].parent == parent)
            continue;
        if (out != in)
            s_widgets[out] = s_widgets[in];
        ++out;
    }
    s_widgetCount = out;
}
