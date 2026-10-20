#include "top_bar_status_update.h"

#include "app_config.h"
#include "can/signal_map.h"
#include "runtime/signal_store.h"
#include "runtime/timer_service.h"
#include "runtime/track_store.h"
#include "ui/status_field_text.h"
#include "ui/widgets/widget_helpers.h"

using namespace TopBarInternal;

namespace {

TimerSources::Inputs timerInputs() {
    TrackStore::State track;
    TrackStore::snapshot(&track);
    return TimerSources::select(TimerService::snapshot(), track,
                                TrackStore::isActiveWithin(kTrackTelemetryTimeoutMs));
}

uint32_t valueColor(bool known) {
    return known ? labelColor() : ThemeManager::getStaleTextColor();
}

}

namespace TopBarStatusUpdate {

void updateTimer(uint8_t, DynItem &d, bool) {
    WidgetHelpers::setVisibleIfChanged(d.obj, true);
    char buf[kDynTextCap];
    const bool known = StatusFieldText::formatTimer(buf, sizeof(buf), d.item, timerInputs());
    applyDynText(d, buf);
    applyDynTextColor(d, valueColor(known));
}

void updateFlagWord(uint8_t, DynItem &d, bool) {
    WidgetHelpers::setVisibleIfChanged(d.obj, true);
    const SignalId sid = signalIdFromName(d.item.signalId);
    const bool valid = sid < SignalIds::kSignalCount && SignalStore::isValid(sid);
    const float value = valid ? SignalStore::read(sid, 0.0f) : 0.0f;
    applyDynText(d, StatusFieldText::flagWord(valid, value, d.item));
    applyDynTextColor(d, valueColor(valid));
}

}
