#include "sim_controls.h"

#include "can/signal_map.h"
#include "runtime/signal_store.h"

#include <lvgl.h>

#include <cstdio>
#include <cstring>

namespace SimControls {

namespace {

struct Variant {
    const char *name;
    float rpm;
    float speedKph;
    float gear;
    float egtC;
    float brake;
    float flags;
    float mapNumber;
    uint8_t taps;
};

constexpr float kNoMapNumber = -1.0f;
constexpr uint32_t kTapDelayMs = 600;

constexpr Variant kVariants[] = {
    {"controls", 950.0f, 0.0f, 0.0f, 700.0f, 0.0f, 0.0f, 1.0f, 0},
    {"controls-armed", 4200.0f, 0.0f, 0.0f, 700.0f, 0.0f, 0.0f, 1.0f, 1},
    {"controls-active", 4200.0f, 0.0f, 0.0f, 700.0f, 0.0f, 1.0f, 1.0f, 1},
    {"controls-locked", 3000.0f, 40.0f, 4.0f, 980.0f, 1.0f, 0.0f, kNoMapNumber, 0},
    {"controls-cruise", 2600.0f, 110.0f, 5.0f, 700.0f, 0.0f, 1.0f, 1.0f, 1},
    {"controls-step", 950.0f, 0.0f, 0.0f, 700.0f, 0.0f, 0.0f, 1.0f, 5},
};

const Variant *s_variant = nullptr;
uint32_t s_startMs = 0;
bool s_tapped = false;

void feed(const Variant &v) {
    SignalStore::update(SignalIds::RPM, v.rpm);
    SignalStore::update(SignalIds::SPEED_KPH, v.speedKph);
    SignalStore::update(SignalIds::GEAR, v.gear);
    SignalStore::update(SignalIds::THROTTLE_POS, 0.0f);
    SignalStore::update(SignalIds::EGT_C, v.egtC);
    SignalStore::update(SignalIds::CLUTCH_STATE, 1.0f);
    SignalStore::update(SignalIds::BATTERY_VOLTS, 13.8f);
    SignalStore::update(SignalIds::COOLANT_TEMP_C, 92.0f);
    SignalStore::update(SignalIds::OIL_PRESS_BAR, 4.1f);
    SignalStore::update(SignalIds::FLAG_BRAKE, v.brake);
    SignalStore::update(SignalIds::FLAG_LAUNCH_CTRL, v.flags);
    SignalStore::update(SignalIds::FLAG_ANTI_LAG, v.flags);
    SignalStore::update(SignalIds::FLAG_PIT_LIMIT, v.flags);
    SignalStore::update(SignalIds::FLAG_CRUISE, v.flags);
    SignalStore::update(SignalIds::FLAG_TRACTION_CUT, v.flags);
    if (v.mapNumber >= 0.0f)
        SignalStore::update(SignalIds::MAP_NUMBER, v.mapNumber);
}

void tapObject(lv_obj_t *obj, uint8_t times) {
    for (uint8_t i = 0; i < times; ++i) {
        lv_event_send(obj, LV_EVENT_PRESSED, nullptr);
        lv_event_send(obj, LV_EVENT_RELEASED, nullptr);
        lv_event_send(obj, LV_EVENT_CLICKED, nullptr);
    }
}

void tapEveryButton(uint8_t times) {
    lv_obj_t *screen = lv_scr_act();
    if (!screen)
        return;
    for (uint32_t i = 0; i < lv_obj_get_child_cnt(screen); ++i) {
        lv_obj_t *child = lv_obj_get_child(screen, i);
        if (!lv_obj_has_flag(child, LV_OBJ_FLAG_CLICKABLE) || !lv_obj_get_user_data(child))
            continue;
        tapObject(child, times);
    }
}

} // namespace

bool select(const char *scenario, uint32_t nowMs) {
    if (!scenario)
        return false;
    for (const Variant &variant : kVariants) {
        if (strcmp(scenario, variant.name) != 0)
            continue;
        s_variant = &variant;
        s_startMs = nowMs;
        s_tapped = false;
        printf("scenario: %s\n", variant.name);
        return true;
    }
    return false;
}

bool active() {
    return s_variant != nullptr;
}

void tick(uint32_t nowMs) {
    if (!s_variant)
        return;
    feed(*s_variant);
    if (s_variant->taps == 0 || s_tapped || nowMs - s_startMs < kTapDelayMs)
        return;
    s_tapped = true;
    tapEveryButton(s_variant->taps);
}

} // namespace SimControls
