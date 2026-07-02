#pragma once

#include <stdint.h>
#include <stdbool.h>

namespace AlertEngine {

enum class AlertLevel : uint8_t { NORMAL = 0, CAUTION = 1, WARNING = 2, CRITICAL = 3 };

struct AlertState {
    AlertLevel revLimiter;
    bool revLimiterFlashActive;
    AlertLevel coolantTemp;
    AlertLevel oilTemp;
    AlertLevel oilPressure;
    bool milActive;
    AlertLevel batteryVoltage;
    AlertLevel global;
    bool coolantSensorLost;
    bool oilTempSensorLost;
    bool oilPressureSensorLost;
    bool batterySensorLost;
};

void init();

void tick();

AlertState getState();

bool isRevLimiterFlashOn();

uint32_t getRevLimiterOverlayColor();

} // namespace AlertEngine
