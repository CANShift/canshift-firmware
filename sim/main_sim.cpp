#include "sim_display.h"
#include "sim_imu.h"
#include "sim_injector.h"

#include "board.h"
#include "can/signal_map.h"
#include "config/board_profile_loader.h"
#include "config/config_loader.h"
#include "diag/logger.h"
#include "diag/lvgl_pool.h"
#include "display_tiers.h"
#include "runtime/alert_engine.h"
#include "runtime/signal_store.h"
#include "ui/font_manager.h"
#include "ui/page_manager.h"
#include "ui/screen_profile.h"
#include "ui/theme_manager.h"

#include <Arduino.h>
#include <lvgl.h>
#include <sim_clock.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

extern void simRegisterLvglFs(const char *rootDir);
extern void simStorageSetRoot(const char *rootDir);

namespace {

constexpr int kWidePanelPx = 640;
constexpr size_t kBoardBlobCapacity = 4096;
constexpr uint32_t kFrameMs = 16;
constexpr uint32_t kCaptureEpochMs = 1000;

int zoomForPanel(int width) {
    return width >= kWidePanelPx ? 1 : 2;
}

bool applyBoardProfileFile(const char *path) {
    FILE *file = fopen(path, "rb");
    if (file == nullptr) {
        printf("board profile '%s' is not readable\n", path);
        return false;
    }
    char blob[kBoardBlobCapacity];
    const size_t len = fread(blob, 1, sizeof blob - 1, file);
    fclose(file);
    blob[len] = '\0';

    if (!canshift::boards::applyBoardProfileBlob(blob, len)) {
        printf("board profile '%s' was rejected\n", path);
        return false;
    }
    return true;
}

void prepareInputs(const char *dataRoot, const char *scenario) {
    simStorageSetRoot(dataRoot);
    simRegisterLvglFs(dataRoot);
    SimImu::prepare(scenario);
}

void applyThemeOverride() {
    const char *theme = getenv("CANSHIFT_SIM_THEME");
    if (theme == nullptr)
        return;
    ThemeManager::setDayMode(strcmp(theme, "day") == 0);
}

}

namespace {

bool bootDashboard(const char *pageId) {
    const ConfigLoader::LoadResult cfg = ConfigLoader::loadAll();
    printf("config loaded: dashboard=%d signals=%d\n", static_cast<int>(cfg.dashboardOk),
           static_cast<int>(cfg.signalsOk));

    ScreenProfile::initFromDashboard();
    FontManager::init();
    LvglPool::report("fonts");
    SignalStore::init();
    AlertEngine::init();
    PageManager::init();
    if (!PageManager::navigateTo(pageId ? pageId : PageManager::getDefaultPageId())) {
        printf("page '%s' not found\n", pageId);
        return false;
    }

    return true;
}

int runLoop(uint32_t captureAfterMs) {
    const uint32_t startMs = millis();
    while (!SimDisplay::quitRequested()) {
        SimDisplay::pumpEvents();

        const uint32_t now = millis();

        SimInjector::tick(now);
        SignalStore::checkTimeouts();

        PageManager::updateWidgets();
        lv_timer_handler();

        if (SimDisplay::screenshotRequested())
            SimDisplay::writeScreenshot("sim-screenshot.bmp");

        if (captureAfterMs > 0 && now - startMs >= captureAfterMs) {
            SimDisplay::writeScreenshot("sim-screenshot.bmp");
            return 0;
        }

        delay(kFrameMs);
    }
    return 0;
}

}

int main(int argc, char **argv) {
    setvbuf(stdout, nullptr, _IOLBF, 0);
    const char *dataRoot = argc > 1 ? argv[1] : "data";
    const char *scenario = argc > 2 ? argv[2] : nullptr;
    const char *pageId = argc > 3 ? argv[3] : nullptr;
    const uint32_t captureAfterMs = argc > 4 ? strtoul(argv[4], nullptr, 10) : 0;
    if (captureAfterMs > 0)
        SimClock::useManual(kCaptureEpochMs);

    const char *boardPath = getenv("CANSHIFT_SIM_BOARD");
    if (boardPath != nullptr && !applyBoardProfileFile(boardPath))
        return 1;

    const int panelW = canshift::display::width();
    const int panelH = canshift::display::height();
    printf("panel %dx%d tier=%s\n", panelW, panelH,
           canshift::display::tierForPanel(static_cast<uint16_t>(panelW),
                                           static_cast<uint16_t>(panelH))
               .id);

    Logger::init();
    lv_init();
    if (!SimDisplay::init(panelW, panelH, zoomForPanel(panelW)))
        return 1;

    prepareInputs(dataRoot, scenario);
    if (!bootDashboard(pageId))
        return 1;

    LvglPool::report("first page build");
    applyThemeOverride();

    SimInjector::init(scenario);

    return runLoop(captureAfterMs);
}
