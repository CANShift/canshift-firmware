#include "day_night_auto.h"
#include "can/signal_map.h"
#include "config/config_loader.h"
#include "diag/logger.h"
#include "runtime/pending_actions.h"
#include "runtime/signal_store.h"

static SignalId s_signalId = SignalIds::SIGNAL_COUNT;
static int8_t s_lastHeadlightState = -1;

static constexpr float HEADLIGHTS_ON_THRESHOLD = 0.5f;

void DayNightAuto::init() {
    s_signalId = SignalIds::SIGNAL_COUNT;
    s_lastHeadlightState = -1;

    const CfgDashboard &dash = ConfigLoader::getDashboardConfig();
    if (dash.dayNightSignal[0] == '\0' || !dash.hasDayTheme)
        return;

    const SignalId sid = signalIdFromName(dash.dayNightSignal);
    if (sid >= SignalIds::SIGNAL_COUNT) {
        LOG_WARN("THEME", "dayNightSignal '%s' unknown — auto day/night disabled",
                 dash.dayNightSignal);
        return;
    }

    s_signalId = sid;
    LOG_INFO("THEME", "Auto day/night bound to signal '%s'", dash.dayNightSignal);
}

void DayNightAuto::tick() {
    if (s_signalId >= SignalIds::SIGNAL_COUNT)
        return;
    if (!SignalStore::isValid(s_signalId))
        return;

    const bool headlightsOn = SignalStore::read(s_signalId) >= HEADLIGHTS_ON_THRESHOLD;
    const int8_t state = headlightsOn ? 1 : 0;
    if (state == s_lastHeadlightState)
        return;

    s_lastHeadlightState = state;
    PendingActions::dayNightSet.store(headlightsOn ? 0 : 1, std::memory_order_relaxed);
}
