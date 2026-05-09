#pragma once
// default_config.h — First-boot provisioning of baked-in default configs.
//
// On a freshly-flashed device with an empty SPIFFS, the firmware writes its
// embedded default dashboard / signals / theme JSON to the canonical paths
// so the dashboard renders out of the box (issue #173).
//
// The default JSON blobs are linked into the firmware via PlatformIO's
// `board_build.embed_files` (see platformio.ini).

#include <stddef.h>
#include <stdint.h>

namespace DefaultConfig {

struct ProvisionResult {
    uint8_t written; // files newly written on this boot
    uint8_t skipped; // files left untouched (already present, or has .bak)
    uint8_t failed;  // files that needed write but the storage write failed
};

/**
 * Provision any missing canonical config files from embedded defaults.
 * Per file, the rule is:
 *   - target missing AND `<target>.bak` missing  → write default
 *   - target present AND size == 0               → write default
 *   - otherwise                                  → skip (preserve user data)
 *
 * Caller MUST ensure StorageDriver::init() succeeded before invoking.
 * Returns counts; never throws, never aborts boot. Failures are logged
 * and pushed to ErrorStore so the studio can surface them.
 */
ProvisionResult provisionMissingFiles();

} // namespace DefaultConfig
