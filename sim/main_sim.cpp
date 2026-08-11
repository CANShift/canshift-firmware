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
    PageManager::navigateTo(PageManager::getDefaultPageId());

    SimInjector::init(scenario);

    uint32_t lastTickMs = millis();
    while (!SimDisplay::quitRequested()) {
        SimDisplay::pumpEvents();

        const uint32_t now = millis();
        lv_tick_inc(now - lastTickMs);
        lastTickMs = now;

        SimInjector::tick(now);
        SignalStore::checkTimeouts();

        static SignalStore::SignalValue snap[SIGNAL_STORE_MAX_SIGNALS];
        SignalStore::snapshotAll(snap);
        AlertEngine::tick(snap);
        PageManager::updateWidgets();
        lv_timer_handler();

        if (SimDisplay::screenshotRequested())
            SimDisplay::writeScreenshot("sim-screenshot.bmp");

        delay(kFrameMs);
    }
    return 0;
}
