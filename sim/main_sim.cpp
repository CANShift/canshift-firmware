#include "sim_display.h"
#include "sim_injector.h"

#include "can/signal_map.h"
#include "config/config_loader.h"
#include "diag/logger.h"
#include "runtime/alert_engine.h"
#include "runtime/signal_store.h"
#include "ui/font_manager.h"
#include "ui/page_manager.h"
#include "ui/screen_profile.h"

#include <Arduino.h>
#include <lvgl.h>

#include <cstdio>

extern void simRegisterLvglFs(const char *rootDir);
extern void simStorageSetRoot(const char *rootDir);

namespace {

constexpr int kPanelW = 320;
constexpr int kPanelH = 240;
constexpr int kZoom = 2;
constexpr uint32_t kFrameMs = 16;

} // namespace

int main(int argc, char **argv) {
    setvbuf(stdout, nullptr, _IOLBF, 0);
    const char *dataRoot = argc > 1 ? argv[1] : "data";
    const char *scenario = argc > 2 ? argv[2] : nullptr;
    const char *pageId = argc > 3 ? argv[3] : nullptr;
    const uint32_t captureAfterMs = argc > 4 ? strtoul(argv[4], nullptr, 10) : 0;

    Logger::init();
    lv_init();
    if (!SimDisplay::init(kPanelW, kPanelH, kZoom))
        return 1;

    simStorageSetRoot(dataRoot);
    simRegisterLvglFs(dataRoot);

    const ConfigLoader::LoadResult cfg = ConfigLoader::loadAll();
    printf("config loaded: dashboard=%d signals=%d\n", static_cast<int>(cfg.dashboardOk),
           static_cast<int>(cfg.signalsOk));

    ScreenProfile::initFromDashboard();
    FontManager::init();
    SignalStore::init();
    AlertEngine::init();
    PageManager::init();
    if (!PageManager::navigateTo(pageId ? pageId : PageManager::getDefaultPageId())) {
        printf("page '%s' not found\n", pageId);
        return 1;
    }

    SimInjector::init(scenario);

    const uint32_t startMs = millis();
    uint32_t lastTickMs = startMs;
    while (!SimDisplay::quitRequested()) {
        SimDisplay::pumpEvents();

        const uint32_t now = millis();
        lv_tick_inc(now - lastTickMs);
        lastTickMs = now;

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
