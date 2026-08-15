#include "bus_health.h"

#include "alert_engine_rs.h"
#include "app_config.h"
#include "can/can_manager.h"

#include <Arduino.h>

namespace BusHealth {

State sample() {
    AlertBusSilenceRs out = {};
    alert_bus_silence_rs(&out, CanManager::msSinceLastRx(), millis(), SIGNAL_DEFAULT_TIMEOUT_MS);
    return State{out.silent, out.seconds};
}

} // namespace BusHealth
