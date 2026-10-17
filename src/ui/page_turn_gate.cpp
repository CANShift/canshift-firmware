#include "ui/page_turn_gate.h"

#include "runtime/workshop_mode.h"
#include "ui/alert_takeover.h"
#include "ui/burn_overlay.h"
#include "ui/control_splash.h"
#include "ui/ota_overlay.h"

bool PageTurnGate::open() {
    if (WorkshopMode::isActive() || WorkshopMode::isEntering())
        return false;
    if (AlertTakeover::isActive() || ControlSplash::isVisible())
        return false;
    return !BurnOverlay::isActive() && !OtaOverlay::isActive();
}
