#include "widget_factory.h"
#include "widgets/gauge_widget.h"
#include "widgets/label_widget.h"
#include "widgets/warning_widget.h"
#include "widgets/button_widget.h"
#include "widgets/gear_widget.h"
#include "widgets/g_meter_widget.h"
#include "widgets/shift_light_widget.h"
#include "widgets/timer_widget.h"
#include "widgets/image_widget.h"
#include "runtime/signal_store.h"
#include "can/signal_map.h"
#include "diag/logger.h"
#include "diag/lvgl_assert_lock.h"
#include "display_tiers.h"

#include <lvgl.h>
#include <stdint.h>

namespace {

static constexpr uint8_t kMaxTrackedWidgets = kConfigMaxPages * kConfigMaxWidgetsPerPage;

static_assert(kConfigMaxWidgetsPerPage == canshift::display::kBaseTier.maxWidgetsPerPage,
              "the per-page cap is the base tier's — a larger tier raises it through its own "
              "entry, not by moving this number");

struct WidgetReading {
    float raw;
    float shown;
    bool valid;
};

using CreateFn = lv_obj_t *(*)(lv_obj_t *, const CfgWidget &, int16_t);
using UpdateFn = void (*)(lv_obj_t *, const WidgetReading &, const CfgWidget &);

struct WidgetOps {
    WidgetType type;
    CreateFn create;
    UpdateFn update;
    bool readsSignal;
};

void updateGauge(lv_obj_t *obj, const WidgetReading &reading, const CfgWidget &cfg) {
    GaugeWidget::update(obj, reading.shown, reading.valid, cfg);
}

void updateLabel(lv_obj_t *obj, const WidgetReading &reading, const CfgWidget &cfg) {
    LabelWidget::update(obj, reading.shown, reading.valid, cfg);
}

void updateWarning(lv_obj_t *obj, const WidgetReading &reading, const CfgWidget &cfg) {
    WarningWidget::update(obj, reading.raw, reading.valid, cfg);
}

void updateGear(lv_obj_t *obj, const WidgetReading &reading, const CfgWidget &cfg) {
    GearWidget::update(obj, reading.raw, reading.valid, cfg);
}

void updateShiftLight(lv_obj_t *obj, const WidgetReading &reading, const CfgWidget &cfg) {
    ShiftLightWidget::update(obj, reading.raw, reading.valid, cfg);
}

void updateTimer(lv_obj_t *obj, const WidgetReading &, const CfgWidget &cfg) {
    TimerWidget::update(obj, 0.0f, false, cfg);
}

void updateGMeter(lv_obj_t *obj, const WidgetReading &, const CfgWidget &) {
    GMeterWidget::update(obj);
}

void updateButton(lv_obj_t *obj, const WidgetReading &, const CfgWidget &) {
    ButtonWidget::update(obj);
}

constexpr WidgetOps kWidgetOps[] = {
    {WidgetType::GAUGE, GaugeWidget::create, updateGauge, true},
    {WidgetType::LABEL, LabelWidget::create, updateLabel, true},
    {WidgetType::WARNING, WarningWidget::create, updateWarning, true},
    {WidgetType::GEAR_IND, GearWidget::create, updateGear, true},
    {WidgetType::SHIFT_LIGHT, ShiftLightWidget::create, updateShiftLight, true},
    {WidgetType::TIMER, TimerWidget::create, updateTimer, false},
    {WidgetType::BUTTON, ButtonWidget::create, updateButton, false},
    {WidgetType::IMAGE, ImageWidget::create, nullptr, false},
    {WidgetType::G_METER, GMeterWidget::create, updateGMeter, false},
};

const WidgetOps *opsFor(WidgetType type) {
    for (const WidgetOps &ops : kWidgetOps) {
        if (ops.type == type)
            return &ops;
    }
    return nullptr;
}

struct WidgetEntry {
    lv_obj_t *parent;
    lv_obj_t *obj;
    const WidgetOps *ops;
    SignalId signalId;
    const CfgWidget *cfg;
};

static WidgetEntry s_widgets[kMaxTrackedWidgets];
static uint8_t s_widgetCount = 0;

bool readSignal(SignalId signalId, const SignalStore::SignalValue snap[kSignalStoreMaxSignals],
                WidgetReading &reading) {
    if (signalId >= SignalIds::kSignalCount || signalId >= kSignalStoreMaxSignals)
        return false;
    const SignalStore::SignalValue &sv = snap[signalId];
    reading.valid = sv.valid;
    reading.raw = sv.valid ? sv.raw : 0.0f;
    reading.shown = sv.valid ? sv.display : 0.0f;
    return true;
}

void updateWidget(const WidgetEntry &entry,
                  const SignalStore::SignalValue snap[kSignalStoreMaxSignals]) {
    if (!entry.ops->update)
        return;
    WidgetReading reading{};
    if (entry.ops->readsSignal && !readSignal(entry.signalId, snap, reading))
        return;
    entry.ops->update(entry.obj, reading, *entry.cfg);
}

SignalId resolveSignal(const CfgWidget &cfg) {
    const SignalId resolved = signalIdFromName(cfg.signalId);
    if (resolved >= SignalIds::kSignalCount && cfg.signalId[0] != '\0') {
        LOG_WARN("WF", "Unknown signal name: %s", cfg.signalId);
    }
    return resolved;
}

}

lv_obj_t *WidgetFactory::create(lv_obj_t *parent, const CfgWidget &cfg, int16_t yOffset) {
    LVGL_ASSERT_LOCKED();
    if (s_widgetCount >= kMaxTrackedWidgets) {
        LOG_ERROR("WF", "Widget registry full — cannot create widget '%s'", cfg.id);
        return nullptr;
    }
    const WidgetOps *ops = opsFor(cfg.type);
    if (!ops) {
        LOG_WARN("WF", "Unknown widget type for '%s'", cfg.id);
        return nullptr;
    }
    lv_obj_t *obj = ops->create(parent, cfg, yOffset);
    if (!obj) {
        LOG_ERROR("WF", "Failed to create widget '%s'", cfg.id);
        return nullptr;
    }
    s_widgets[s_widgetCount++] = WidgetEntry{parent, obj, ops, resolveSignal(cfg), &cfg};
    LOG_DEBUG("WF", "Created widget '%s' type=%d at (%d,%d)", cfg.id, cfg.type, cfg.layout.x,
              cfg.layout.y + yOffset);
    return obj;
}

void WidgetFactory::updateAll(lv_obj_t *parent, const SignalStore::SignalValue *snap) {
    LVGL_ASSERT_LOCKED();
    for (uint8_t i = 0; i < s_widgetCount; ++i) {
        if (s_widgets[i].parent == parent) {
            updateWidget(s_widgets[i], snap);
        }
    }
}

void WidgetFactory::clearAll(lv_obj_t *parent) {
    LVGL_ASSERT_LOCKED();
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
