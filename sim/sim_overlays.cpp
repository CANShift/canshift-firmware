#include "sim_overlays.h"

#include "diag/error_store.h"
#include "ui/boot_screens.h"
#include "ui/config_rejected_screen.h"
#include "ui/no_config_screen.h"
#include "ui/ota_overlay.h"
#include "ui/workshop_screen.h"

#include <lvgl.h>

#include <cstdio>
#include <cstring>

namespace {

bool s_otaDemo = false;
uint32_t s_otaStartMs = 0;

constexpr size_t kOtaTotalBytes = 1024 * 1024;
constexpr uint32_t kOtaDurationMs = 12000;
constexpr uint32_t kOtaFailDetail = 0x1502;
constexpr char kOtaTargetVersion[] = "0.1.1";
constexpr uint8_t kRejectedPage = 4;
constexpr char kRejectedWidgetId[] = "egt";
constexpr int16_t kRejectedWidgetY = 208;
constexpr int16_t kRejectedWidgetH = 52;
constexpr int16_t kRejectedMaxY = 240;

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
    {"workshop", startWorkshop},    {"workshop-oil", startWorkshop},
    {"ota-oil", startOtaDemo}};

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

}

namespace SimOverlays {

void start(const char *scenario, uint32_t nowMs) {
    for (const OverlayScenario &entry : kOverlayScenarios) {
        if (strcmp(scenario, entry.name) == 0) {
            entry.start(nowMs);
            printf("scenario: %s\n", entry.name);
        }
    }
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

void tick(uint32_t nowMs) {
    tickOtaDemo(nowMs);
}

}
