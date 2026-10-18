#include "sim_injector.h"
#include "sim_can_bus.h"
#include "sim_controls.h"
#include "sim_display.h"
#include "sim_splash.h"

#include "can/signal_map.h"
#include "diag/error_store.h"
#include "runtime/signal_store.h"
#include "ui/boot_screens.h"
#include "ui/config_rejected_screen.h"
#include "ui/no_config_screen.h"
#include "ui/ota_overlay.h"
#include "ui/page_manager.h"
#include "ui/workshop_screen.h"

#include <Arduino.h>
#include <SDL.h>

#include <lvgl.h>

#include <cmath>
#include <cstdio>
#include <cstring>

namespace {

enum class Mode : uint8_t {
    Cruise,
    RevLimit,
    RevRelease,
    OilCritical,
    OilLow,
    WaterCritical,
    Warning,
    AllStale,
    BusLost,
    CutBoost,
    CutKnock,
    CutFuel,
    CutLimp
};

struct ScenarioName {
    const char *name;
    Mode mode;
};

constexpr ScenarioName kScenarioNames[] = {
    {"cruise", Mode::Cruise},      {"rev", Mode::RevLimit},       {"rev-release", Mode::RevRelease},
    {"oil", Mode::OilCritical},    {"oil-low", Mode::OilLow},     {"water", Mode::WaterCritical},
    {"warn", Mode::Warning},       {"stale", Mode::AllStale},     {"bus-lost", Mode::BusLost},
    {"cut-boost", Mode::CutBoost}, {"cut-knock", Mode::CutKnock}, {"cut-fuel", Mode::CutFuel},
    {"cut-limp", Mode::CutLimp}};

Mode s_mode = Mode::Cruise;
uint32_t s_startMs = 0;
bool s_otaDemo = false;
uint32_t s_otaStartMs = 0;

constexpr float kRevLimitRpm = 7400.0f;
constexpr uint32_t kRevReleaseMs = 2000;
constexpr size_t kOtaTotalBytes = 1024 * 1024;
constexpr uint32_t kOtaDurationMs = 12000;
constexpr uint32_t kOtaFailDetail = 0x1502;
constexpr char kOtaTargetVersion[] = "0.1.1";
constexpr float kOilLowBar = 1.2f;
constexpr float kOilCriticalBar = 0.4f;
constexpr float kOilCriticalRpm = 5200.0f;
constexpr float kWaterCriticalC = 118.0f;
constexpr float kWaterCriticalRpm = 4100.0f;
constexpr float kWarnCoolantC = 105.0f;
constexpr float kWarnOilTempC = 138.0f;
constexpr float kWarnIatC = 58.0f;
constexpr uint32_t kBusLostFeedMs = 1000;
constexpr float kBusLostBatteryVolts = 13.8f;
constexpr uint8_t kRejectedPage = 4;
constexpr char kRejectedWidgetId[] = "egt";
constexpr int16_t kRejectedWidgetY = 208;
constexpr int16_t kRejectedWidgetH = 52;
constexpr int16_t kRejectedMaxY = 240;

struct Pin {
    SignalId id;
    float value;
};

template <size_t N>
constexpr size_t pinCount(const Pin (&)[N]) {
    return N;
}

const Pin *s_pins = nullptr;
size_t s_pinCount = 0;

void setSignal(SignalId id, float value) {
    for (size_t i = 0; i < s_pinCount; ++i) {
        if (s_pins[i].id != id)
            continue;
        SignalStore::update(id, s_pins[i].value);
        return;
    }
    SignalStore::update(id, value);
}

void feedCruise(uint32_t nowMs, float oilPressBar = 4.1f, float rpmOverride = -1.0f,
                float coolantC = 92.0f) {
    SimCanBus::markRx();
    const float t = static_cast<float>(nowMs - s_startMs) / 1000.0f;
    const float sweep = 0.5f + 0.5f * sinf(t * 0.6f);
    setSignal(SignalIds::kRpm, rpmOverride >= 0.0f ? rpmOverride : 1200.0f + sweep * 4800.0f);
    setSignal(SignalIds::kSpeedKph, 40.0f + sweep * 140.0f);
    setSignal(SignalIds::kGear, 2.0f + floorf(sweep * 4.0f));
    setSignal(SignalIds::kThrottlePos, sweep * 100.0f);
    setSignal(SignalIds::kBoostBar, sweep * 1.4f);
    setSignal(SignalIds::kMapKpa, 100.0f + sweep * 120.0f);
    setSignal(SignalIds::kCoolantTempC, coolantC);
    setSignal(SignalIds::kOilTempC, 104.0f);
    setSignal(SignalIds::kOilPressBar, oilPressBar);
    setSignal(SignalIds::kFuelPressBar, 3.0f);
    setSignal(SignalIds::kBatteryVolts, 13.8f);
    setSignal(SignalIds::kIatC, 38.0f);
    setSignal(SignalIds::kLambda1, 0.95f + 0.05f * sinf(t * 2.0f));
    setSignal(SignalIds::kAfr1, 14.0f + sweep * 0.9f);
    setSignal(SignalIds::kFuelLevelPct, 46.0f);
    setSignal(SignalIds::kEgtC, 780.0f + sweep * 180.0f);
    setSignal(SignalIds::kGearboxTempC, 96.0f);
    setSignal(SignalIds::kDiffTempC, 104.0f);
    setSignal(SignalIds::kKnockCount, 0.0f);
    setSignal(SignalIds::kClutchState, 0.0f);
    setSignal(SignalIds::kOdoKm, 184203.0f);
    setSignal(SignalIds::kTripKm, 128.0f);
    setSignal(SignalIds::kMapNumber, 1.0f);
}

void feedRevLimit(uint32_t nowMs) {
    feedCruise(nowMs, 4.1f, kRevLimitRpm);
}

void feedRevRelease(uint32_t nowMs) {
    if (nowMs - s_startMs < kRevReleaseMs) {
        feedRevLimit(nowMs);
        return;
    }
    feedCruise(nowMs);
}

void feedOilCritical(uint32_t nowMs) {
    feedCruise(nowMs, kOilCriticalBar, kOilCriticalRpm);
}

void feedOilLow(uint32_t nowMs) {
    feedCruise(nowMs, kOilLowBar);
}

void feedWaterCritical(uint32_t nowMs) {
    feedCruise(nowMs, 4.1f, kWaterCriticalRpm, kWaterCriticalC);
}

void feedWarning(uint32_t nowMs) {
    feedCruise(nowMs);
    SignalStore::update(SignalIds::kCoolantTempC, kWarnCoolantC);
    SignalStore::update(SignalIds::kOilTempC, kWarnOilTempC);
    SignalStore::update(SignalIds::kIatC, kWarnIatC);
}

void feedBusLost(uint32_t nowMs) {
    if (nowMs - s_startMs < kBusLostFeedMs) {
        feedCruise(nowMs);
        return;
    }
    SignalStore::update(SignalIds::kBatteryVolts, kBusLostBatteryVolts);
}

constexpr Pin kBoostCutPins[] = {
    {SignalIds::kBoostBar, 1.94f}, {SignalIds::kThrottlePos, 100.0f}, {SignalIds::kMapKpa, 294.0f}};

constexpr Pin kKnockPins[] = {{SignalIds::kKnockCount, 4.0f},
                              {SignalIds::kIatC, 52.0f},
                              {SignalIds::kLambda1, 0.84f},
                              {SignalIds::kEgtC, 948.0f}};

constexpr Pin kFuelCutPins[] = {{SignalIds::kOilTempC, 128.0f}, {SignalIds::kCoolantTempC, 104.0f}};

constexpr Pin kLimpPins[] = {{SignalIds::kBoostBar, 0.0f}, {SignalIds::kMapKpa, 100.0f}};

void feedPinned(uint32_t nowMs, const Pin *pins, size_t count, float oilPressBar, float rpm) {
    s_pins = pins;
    s_pinCount = count;
    feedCruise(nowMs, oilPressBar, rpm);
    s_pins = nullptr;
    s_pinCount = 0;
}

void feedCutBoost(uint32_t nowMs) {
    feedPinned(nowMs, kBoostCutPins, pinCount(kBoostCutPins), 4.1f, 6180.0f);
    SignalStore::update(SignalIds::kBoostTargetBar, 1.80f);
    SignalStore::update(SignalIds::kFlagBoostCut, 1.0f);
}

void feedCutKnock(uint32_t nowMs) {
    feedPinned(nowMs, kKnockPins, pinCount(kKnockPins), 4.1f, -1.0f);
    SignalStore::update(SignalIds::kKnockCyl, 3.0f);
    SignalStore::update(SignalIds::kFlagIgnitionRetard, 1.0f);
}

void feedCutFuel(uint32_t nowMs) {
    feedPinned(nowMs, kFuelCutPins, pinCount(kFuelCutPins), 1.1f, 2400.0f);
    SignalStore::update(SignalIds::kFlagFuelCut, 1.0f);
}

void feedCutLimp(uint32_t nowMs) {
    feedPinned(nowMs, kLimpPins, pinCount(kLimpPins), 4.1f, 3000.0f);
    SignalStore::update(SignalIds::kFlagLimpMode, 1.0f);
}

void startOtaDemo(uint32_t nowMs) {
    s_otaDemo = true;
    s_otaStartMs = nowMs;
    OtaOverlay::show(kOtaTotalBytes, kOtaTargetVersion);
}

void startOtaComplete(uint32_t) {
    OtaOverlay::showComplete();
}

void startOtaFailed(uint32_t) {
    OtaOverlay::showFailed(OtaOverlay::FailReason::Commit, kOtaFailDetail);
}

void hideDashChrome() {
    lv_obj_add_flag(lv_layer_top(), LV_OBJ_FLAG_HIDDEN);
}

void startNoConfig(uint32_t) {
    hideDashChrome();
    NoConfigScreen::show();
}

void startConfigRejected(uint32_t) {
    hideDashChrome();
    CfgRejection rejection = {};
    rejection.present = true;
    rejection.pageNumber = kRejectedPage;
    strncpy(rejection.widgetId, kRejectedWidgetId, sizeof(rejection.widgetId) - 1);
    rejection.widgetY = kRejectedWidgetY;
    rejection.widgetH = kRejectedWidgetH;
    rejection.maxY = kRejectedMaxY;
    ConfigRejectedScreen::show(rejection);
}

void startWorkshop(uint32_t) {
    if (!WorkshopScreen::enter())
        printf("workshop card could not be entered\n");
}

void startFailureSurface(uint32_t) {
    ErrorStore::push(ERROR_SRC_CONFIG, "OVERFLOW", "PAGE 4 EXCEEDS 240 PX");
    ErrorStore::push(ERROR_SRC_CAN, "NO_FRAMES", "BUS SILENT 4 s — CHECK WIRING");
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
    {"boot", startBootScreen},      {"self-test", startSelfTestScreen},
    {"no-config", startNoConfig},   {"config-rejected", startConfigRejected},
    {"workshop", startWorkshop}};

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

void feedNothing(uint32_t) {}

void feedCruiseDefaults(uint32_t nowMs) {
    feedCruise(nowMs);
}

constexpr void (*kFeeders[])(uint32_t nowMs) = {
    feedCruiseDefaults, feedRevLimit, feedRevRelease, feedOilCritical, feedOilLow,
    feedWaterCritical,  feedWarning,  feedNothing,    feedBusLost,     feedCutBoost,
    feedCutKnock,       feedCutFuel,  feedCutLimp,
};

static_assert(sizeof(kFeeders) / sizeof(kFeeders[0]) == static_cast<size_t>(Mode::CutLimp) + 1,
              "every sim mode needs a feeder");

struct ModeKey {
    int key;
    Mode mode;
    const char *label;
};

constexpr ModeKey kModeKeys[] = {
    {SDLK_c, Mode::Cruise, "cruise"},
    {SDLK_r, Mode::RevLimit, "rev limit"},
    {SDLK_o, Mode::OilCritical, "oil pressure critical"},
    {SDLK_w, Mode::Warning, "warning band"},
    {SDLK_1, Mode::CutBoost, "boost cut band"},
    {SDLK_2, Mode::CutKnock, "ignition retard band"},
    {SDLK_3, Mode::CutFuel, "fuel cut band"},
    {SDLK_4, Mode::CutLimp, "limp mode band"},
    {SDLK_x, Mode::AllStale, "all signals stale"},
    {SDLK_b, Mode::BusLost, "bus lost"},
};

bool selectMode(int key, uint32_t nowMs) {
    for (const ModeKey &entry : kModeKeys) {
        if (entry.key != key)
            continue;
        s_mode = entry.mode;
        if (entry.mode == Mode::BusLost)
            s_startMs = nowMs;
        printf("mode: %s\n", entry.label);
        return true;
    }
    return false;
}

void toggleOtaDemo(uint32_t nowMs) {
    if (s_otaDemo) {
        s_otaDemo = false;
        OtaOverlay::hide();
        printf("ota demo: hidden\n");
        return;
    }
    startOtaDemo(nowMs);
    printf("ota demo: started\n");
}

void dumpScreenTrees(uint32_t) {
    printf("=== screen tree ===\n");
    dumpObj(lv_scr_act(), 0);
    printf("=== top layer ===\n");
    dumpObj(lv_layer_top(), 0);
}

void showNextPage(uint32_t) {
    PageManager::navigateNext();
}

void showPrevPage(uint32_t) {
    PageManager::navigatePrev();
}

struct ActionKey {
    int key;
    void (*run)(uint32_t nowMs);
};

constexpr ActionKey kActionKeys[] = {
    {SDLK_f, toggleOtaDemo},
    {SDLK_d, dumpScreenTrees},
    {SDLK_RIGHT, showNextPage},
    {SDLK_LEFT, showPrevPage},
};

void handleKey(int key, uint32_t nowMs) {
    if (selectMode(key, nowMs))
        return;
    for (const ActionKey &entry : kActionKeys) {
        if (entry.key == key) {
            entry.run(nowMs);
            return;
        }
    }
}

}

namespace SimInjector {

void init(const char *scenario) {
    s_startMs = millis();
    printf("keys: C cruise · R rev-limit · O oil-critical · W warning · X stale · B bus-lost · "
           "F ota · 1-4 cut bands · ←/→ pages · S screenshot · ESC quit\n");
    if (!scenario)
        return;
    for (const ScenarioName &entry : kScenarioNames) {
        if (strcmp(scenario, entry.name) == 0) {
            s_mode = entry.mode;
            printf("scenario: %s\n", entry.name);
        }
    }
    if (SimControls::select(scenario, s_startMs))
        return;
    if (SimSplash::select(scenario, s_startMs))
        return;
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

    if (SimControls::active()) {
        SimControls::tick(nowMs);
        return;
    }

    if (SimSplash::active()) {
        SimSplash::tick(nowMs);
        return;
    }

    kFeeders[static_cast<uint8_t>(s_mode)](nowMs);
}

}
