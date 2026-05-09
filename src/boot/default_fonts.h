#pragma once
// default_fonts.h — First-boot provisioning of baked-in Orbitron .bin fonts.
//
// PR #431 introduced 8 SPIFFS-resident Orbitron .bin glyphs loaded by
// FontManager::init(). On a freshly-flashed device that ran only
// `pio run -t upload` (no `uploadfs`), and on OTA upgrades that replace only
// the app partition, those .bin files are absent — every weight/size falls
// back to the in-flash 14 px Medium glyph and the dashboard renders unreadable.
//
// To guarantee any successful firmware boot has the full font set on SPIFFS,
// the 8 .bin files are linked into the firmware via PlatformIO's
// `board_build.embed_files` (see platformio.ini) and written to their
// canonical paths on boot when missing — symmetric to default_config.cpp
// (issue #467).

#include <stddef.h>
#include <stdint.h>

namespace DefaultFonts {

struct ProvisionResult {
    uint8_t written; // .bin files newly written on this boot
    uint8_t skipped; // .bin files left untouched (already present)
    uint8_t failed;  // .bin files that needed write but the storage write failed
};

/**
 * Provision any missing canonical Orbitron .bin font files from embedded
 * defaults. Per file, the rule is:
 *   - target missing  → write embedded default
 *   - target present  → skip (assume valid; FontManager handles bad blobs)
 *
 * Caller MUST ensure StorageDriver::init() succeeded before invoking AND
 * must invoke this BEFORE FontManager::init() so lv_font_load() finds the
 * files on SPIFFS. Returns counts; never throws, never aborts boot.
 * Failures are logged and pushed to ErrorStore so the studio can surface them.
 */
ProvisionResult provisionMissingFiles();

} // namespace DefaultFonts
