#pragma once
// sim_engine.h — Simulated signal generator for UI development
//
// Active only when APP_SIMULATION_MODE == 1 (set in app_config.h or platformio.ini).
// Generates realistic VR6 engine data without a live CAN connection.
// Writes to SignalStore exactly as the CAN parser would.

namespace SimEngine {

/**
     * Initialize simulation state (RPM sweep direction, counters, etc.).
     */
void init();

/**
     * Advance simulation one tick and update SignalStore.
     * Call from the simulation FreeRTOS task at SIM_UPDATE_MS intervals.
     */
void tick();

} // namespace SimEngine
