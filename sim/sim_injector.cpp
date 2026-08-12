#include "sim_injector.h"
#include "sim_display.h"

#include "can/signal_map.h"
#include "runtime/signal_store.h"
#include "ui/ota_overlay.h"
#include "ui/page_manager.h"

#include <Arduino.h>
#include <SDL.h>

#include <lvgl.h>

#include <cmath>
#include <cstdio>
#include <cstring>

namespace {

enum class Mode : uint8_t { Cruise, RevLimit, OilCritical, AllStale };

struct ScenarioName {
    const char *name;
    Mode mode;
};

constexpr ScenarioName kScenarioNames[] = {{"cruise", Mode::Cruise},
                                           {"rev", Mode::RevLimit},
                                           {"oil", Mode::OilCritical},
                                           {"stale", Mode::AllStale}};

Mode s_mode = Mode::Cruise;
uint32_t s_startMs = 0;
bool s_otaDemo = false;
uint32_t s_otaStartMs = 0;

constexpr float kRevLimitRpm = 7400.0f;
constexpr size_t kOtaTotalBytes = 1024 * 1024;
constexpr uint32_t kOtaDurationMs = 12000;

void feedCruise(uint32_t nowMs, float oilPressBar = 4.1f, float rpmOverride = -1.0f) {
    const float t = static_cast<float>(nowMs - s_startMs) / 1000.0f;
    const float sweep = 0.5f + 0.5f * sinf(t * 0.6f);
    SignalStore::update(SignalIds::RPM,
                        rpmOverride >= 0.0f ? rpmOverride : 1200.0f + sweep * 4800.0f);
    SignalStore::update(SignalIds::SPEED_KPH, 40.0f + sweep * 140.0f);
    SignalStore::update(SignalIds::GEAR, 2.0f + floorf(sweep * 4.0f));
    SignalStore::update(SignalIds::THROTTLE_POS, sweep * 100.0f);
    SignalStore::update(SignalIds::BOOST_BAR, sweep * 1.4f);
    SignalStore::update(SignalIds::MAP_KPA, 100.0f + sweep * 120.0f);
    SignalStore::update(SignalIds::COOLANT_TEMP_C, 92.0f);
    SignalStore::update(SignalIds::OIL_TEMP_C, 104.0f);
    SignalStore::update(SignalIds::OIL_PRESS_BAR, oilPressBar);
    SignalStore::update(SignalIds::FUEL_PRESS_BAR, 3.0f);
    SignalStore::update(SignalIds::BATTERY_VOLTS, 13.8f);
    SignalStore::update(SignalIds::IAT_C, 38.0f);
    SignalStore::update(SignalIds::LAMBDA_1, 0.95f + 0.05f * sinf(t * 2.0f));
    SignalStore::update(SignalIds::AFR_1, 14.0f + sweep * 0.9f);
    SignalStore::update(SignalIds::FUEL_LEVEL_PCT, 46.0f);
    SignalStore::update(SignalIds::EGT_C, 780.0f + sweep * 180.0f);
    SignalStore::update(SignalIds::GEARBOX_TEMP_C, 96.0f);
    SignalStore::update(SignalIds::DIFF_TEMP_C, 104.0f);
    SignalStore::update(SignalIds::KNOCK_COUNT, 0.0f);
    SignalStore::update(SignalIds::CLUTCH_STATE, 0.0f);
    SignalStore::update(SignalIds::ODO_KM, 184203.0f);
    SignalStore::update(SignalIds::TRIP_KM, 128.0f);
    SignalStore::update(SignalIds::MAP_NUMBER, 1.0f);
}

void feedRevLimit(uint32_t nowMs) {
    feedCruise(nowMs, 4.1f, kRevLimitRpm);
}

void feedOilCritical(uint32_t nowMs) {
    feedCruise(nowMs, 0.4f);
}

void tickOtaDemo(uint32_t nowMs) {
    if (!s_otaDemo)
        return;
    const uint32_t elapsed = nowMs - s_otaStartMs;
    if (elapsed >= kOtaDurationMs) {
        OtaOverlay::hide();
        s_otaDemo = false;
        return;
    }
    OtaOverlay::setProgress(
        static_cast<size_t>((static_cast<uint64_t>(kOtaTotalBytes) * elapsed) / kOtaDurationMs));
    OtaOverlay::Detail::tick();
}

void dumpObj(lv_obj_t *obj, int depth) {
    lv_area_t coords;
    lv_obj_get_coords(obj, &coords);
    printf("%*s obj=%p x=%d y=%d w=%d h=%d hidden=%d\n", depth * 2, "", (void *)obj, coords.x1,
           coords.y1, lv_area_get_width(&coords), lv_area_get_height(&coords),
           lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN) ? 1 : 0);
    if (depth >= 3)
        return;
    for (uint32_t i = 0; i < lv_obj_get_child_cnt(obj); ++i)
        dumpObj(lv_obj_get_child(obj, i), depth + 1);
}

void handleKey(int key, uint32_t nowMs) {
    switch (key) {
        case SDLK_c:
            s_mode = Mode::Cruise;
            printf("mode: cruise\n");
            break;
        case SDLK_r:
            s_mode = Mode::RevLimit;
            printf("mode: rev limit\n");
            break;
        case SDLK_o:
            s_mode = Mode::OilCritical;
            printf("mode: oil pressure critical\n");
            break;
        case SDLK_x:
            s_mode = Mode::AllStale;
            printf("mode: all signals stale\n");
            break;
        case SDLK_f:
            s_otaDemo = !s_otaDemo;
            if (s_otaDemo) {
                s_otaStartMs = nowMs;
                OtaOverlay::show(kOtaTotalBytes);
                printf("ota demo: started\n");
            } else {
                OtaOverlay::hide();
                printf("ota demo: hidden\n");
            }
            break;
        case SDLK_d:
            printf("=== screen tree ===\n");
            dumpObj(lv_scr_act(), 0);
            printf("=== top layer ===\n");
            dumpObj(lv_layer_top(), 0);
            break;
        case SDLK_RIGHT:
            PageManager::navigateNext();
            break;
        case SDLK_LEFT:
            PageManager::navigatePrev();
            break;
        default:
            break;
    }
}

} // namespace

namespace SimInjector {

void init(const char *scenario) {
    s_startMs = millis();
    for (const ScenarioName &entry : kScenarioNames) {
        if (scenario && strcmp(scenario, entry.name) == 0) {
            s_mode = entry.mode;
            printf("scenario: %s\n", entry.name);
        }
    }
    printf("keys: C cruise · R rev-limit · O oil-critical · X stale · F ota · ←/→ pages · S "
           "screenshot · ESC quit\n");
}

void tick(uint32_t nowMs) {
    int key;
    while ((key = SimDisplay::pollKey()) != 0)
        handleKey(key, nowMs);

    tickOtaDemo(nowMs);

    switch (s_mode) {
        case Mode::Cruise:
            feedCruise(nowMs);
            break;
        case Mode::RevLimit:
            feedRevLimit(nowMs);
            break;
        case Mode::OilCritical:
            feedOilCritical(nowMs);
            break;
        case Mode::AllStale:
            break;
    }
}

} // namespace SimInjector
