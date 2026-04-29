#pragma once
// config_loader.h — Load and parse all config JSON files from storage
//
// Reads dashboard.json and signals.json from SPIFFS/SD.
// Parses JSON using ArduinoJson and populates the CfgDashboard and CfgSignalConfig
// structs. Falls back to built-in defaults on parse failure.

#include "config_types.h"

namespace ConfigLoader {

struct LoadResult {
    bool dashboardOk;
    bool signalsOk;
};

/**
     * Load all config files.
     * Returns status for each file.
     * On failure, the corresponding cfg struct has .loaded = false and
     * callers should use built-in defaults.
     */
LoadResult loadAll();

/**
     * Access the loaded dashboard config.
     * Returns a reference valid for the firmware lifetime.
     */
const CfgDashboard &getDashboardConfig();

/**
     * Access the loaded signal config.
     */
const CfgSignalConfig &getSignalConfig();

/**
     * Reload all configs from storage and rebuild the UI.
     * Called after USB comm receives a PUT_CONFIG command.
     * NOTE: Must be called from the UI task context (holds LVGL mutex).
     */
bool reloadAll();

} // namespace ConfigLoader
