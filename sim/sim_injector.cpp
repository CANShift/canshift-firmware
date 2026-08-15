#include "sim_injector.h"
#include "sim_can_bus.h"
#include "sim_display.h"

#include "can/signal_map.h"
#include "diag/error_store.h"
#include "runtime/signal_store.h"
#include "ui/boot_screens.h"
#include "ui/ota_overlay.h"
#include "ui/page_manager.h"

#include <Arduino.h>
#include <SDL.h>

#include <lvgl.h>

#include <cmath>
#include <cstdio>
#include <cstring>

namespace {

enum class Mode : uint8_t { Cruise, RevLimit, OilCritical, OilLow, Warning, AllStale, BusLost };

struct ScenarioName {
    const char *name;
    Mode mode;
};

constexpr ScenarioName kScenarioNames[] = {{"cruise", Mode::Cruise},   {"rev", Mode::RevLimit},
                                           {"oil", Mode::OilCritical}, {"oil-low", Mode::OilLow},
                                           {"warn", Mode::Warning},    {"stale", Mode::AllStale},
                                           {"bus-lost", Mode::BusLost}};

Mode s_mode = Mode::Cruise;
uint32_t s_startMs = 0;
bool s_otaDemo = false;
uint32_t s_otaStartMs = 0;

constexpr float kRevLimitRpm = 7400.0f;
constexpr size_t kOtaTotalBytes = 1024 * 1024;
constexpr uint32_t kOtaDurationMs = 12000;
constexpr uint32_t kOtaFailDetail = 0x1502;
constexpr float kOilLowBar = 1.2f;
constexpr float kWarnCoolantC = 105.0f;
constexpr float kWarnOilTempC = 138.0f;
constexpr float kWarnIatC = 58.0f;
constexpr uint32_t kBusLostFeedMs = 1000;
constexpr float kBusLostBatteryVolts = 13.8f;

void feedCruise(uint32_t nowMs, float oilPressBar = 4.1f, float rpmOverride = -1.0f) {
    SimCanBus::markRx();
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

void feedOilLow(uint32_t nowMs) {
    feedCruise(nowMs, kOilLowBar);
}

void feedWarning(uint32_t nowMs) {
    feedCruise(nowMs);
    SignalStore::update(SignalIds::COOLANT_TEMP_C, kWarnCoolantC);
    SignalStore::update(SignalIds::OIL_TEMP_C, kWarnOilTempC);
    SignalStore::update(SignalIds::IAT_C, kWarnIatC);
}

void feedBusLost(uint32_t nowMs) {
    if (nowMs - s_startMs < kBusLostFeedMs) {
        feedCruise(nowMs);
        return;
    }
    SignalStore::update(SignalIds::BATTERY_VOLTS, kBusLostBatteryVolts);
}

void startOtaDemo(uint32_t nowMs) {
    s_otaDemo = true;
    s_otaStartMs = nowMs;
    OtaOverlay::show(kOtaTotalBytes);
}

void startOtaComplete(uint32_t) {
    OtaOverlay::showComplete();
}

void startOtaFailed(uint32_t) {
    OtaOverlay::showFailed(OtaOverlay::FailReason::Commit, kOtaFailDetail);
}

void startFailureSurface(uint32_t) {
    ErrorStore::push(ERROR_SRC_CONFIG, "OVERFLOW", "PAGE 4 EXCEEDS 240 PX");
    ErrorStore::push(ERROR_SRC_CAN, "NO_FRAMES", "BUS SILENT 4 s - CHECK WIRING");
}

lv_obj_t *makeFullScreenOverlay() {
    lv_obj_t *root = lv_obj_create(lv_layer_top());
    if (!root)
        return nullptr;
    lv_obj_set_size(root, LV_HOR_RES, LV_VER_RES);
    lv_obj_center(root);
    return root;
}

void startBootScreen(uint32_t) {
    BootScreens::buildBoot(makeFullScreenOverlay());
}

void startSelfTestScreen(uint32_t) {
    constexpr BootScreens::CheckResult kSelfTest[BootScreens::kCheckCount] = {
        {"OK", true}, {"OK", true}, {"OK", true}, {"6 PAGES", true}, {"NO FRAMES", false}};
    BootScreens::buildSelfTest(makeFullScreenOverlay(), kSelfTest);
}

struct OverlayScenario {
    const char *name;
    void (*start)(uint32_t nowMs);
};

constexpr OverlayScenario kOverlayScenarios[] = {
    {"ota", startOtaDemo},          {"ota-complete", startOtaComplete},
    {"ota-failed", startOtaFailed}, {"failure", startFailureSurface},
    {"boot", startBootScreen},      {"self-test", startSelfTestScreen}};

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
        case SDLK_w:
            s_mode = Mode::Warning;
            printf("mode: warning band\n");
            break;
        case SDLK_x:
            s_mode = Mode::AllStale;
            printf("mode: all signals stale\n");
            break;
        case SDLK_b:
            s_mode = Mode::BusLost;
            s_startMs = nowMs;
            printf("mode: bus lost\n");
            break;
        case SDLK_f:
            if (s_otaDemo) {
                s_otaDemo = false;
                OtaOverlay::hide();
                printf("ota demo: hidden\n");
            } else {
                startOtaDemo(nowMs);
                printf("ota demo: started\n");
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
    printf("keys: C cruise · R rev-limit · O oil-critical · W warning · X stale · B bus-lost · "
           "F ota · ←/→ pages · S screenshot · ESC quit\n");
    if (!scenario)
        return;
    for (const ScenarioName &entry : kScenarioNames) {
        if (strcmp(scenario, entry.name) == 0) {
            s_mode = entry.mode;
            printf("scenario: %s\n", entry.name);
        }
    }
    for (const OverlayScenario &entry : kOverlayScenarios) {
        if (strcmp(scenario, entry.name) == 0) {
            entry.start(s_startMs);
            printf("scenario: %s\n", entry.name);
        }
    }
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
        case Mode::OilLow:
            feedOilLow(nowMs);
            break;
        case Mode::Warning:
            feedWarning(nowMs);
            break;
        case Mode::AllStale:
            break;
        case Mode::BusLost:
            feedBusLost(nowMs);
            break;
    }
}

} // namespace SimInjector
