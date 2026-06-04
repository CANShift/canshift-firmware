#pragma once
// config_loader_internal.h — Cross-file forward decls for the ConfigLoader
// translation units (#1207).
//
// Same pattern as `settings_page_internal.h` (PR #1321). The public API in
// `config_loader.h` stays byte-identical; this header is consumed only by the
// orchestrator + helper + validator + parser TUs and is NEVER included by
// callers outside `src/config/`.
//
// Owns:
//   - `extern` decls for the file-static config storage (`s_dashboard`,
//     `s_signals`, `s_device`, `s_inputs`) — definitions live in
//     `config_loader.cpp` (orchestrator).
//   - the shared rollback snapshot accessor `acquireRollbackSnapshot()`.
//   - cross-module entry points: the four loaders called by the orchestrator,
//     plus the parsers / validators called between the helper TUs.
//
// Wrapped in the `ConfigLoaderInternal` namespace so the symbols can't leak
// into unrelated translation units that #include this header by mistake.

#include "config_types.h"

#include <ArduinoJson.h>
#include <cstddef>
#include <cstdint>

namespace ConfigLoaderInternal {

// ---------------------------------------------------------------------------
// Shared config storage (defined in config_loader.cpp)
// ---------------------------------------------------------------------------

extern CfgDashboard s_dashboard;
extern CfgSignalConfig s_signals;
extern CfgDeviceConfig s_device;
extern CfgInputBindings s_inputs;

// Rollback snapshot — returns the buffer or nullptr when no backing storage
// is available (WROOM with PSRAM env). See config_loader.cpp for rationale.
[[nodiscard]] uint8_t *acquireRollbackSnapshot();

// ---------------------------------------------------------------------------
// File loaders (config_file_loaders.cpp) — driven by ConfigLoader::loadAll()
// ---------------------------------------------------------------------------

[[nodiscard]] bool loadDashboard();
[[nodiscard]] bool loadSignals();
[[nodiscard]] bool loadDevice();
[[nodiscard]] bool loadInputBindings();

// ---------------------------------------------------------------------------
// JSON helpers (json_helpers.cpp)
// ---------------------------------------------------------------------------

// Read + parse JSON from `path`, falling back to `<path>.bak` on a missing or
// corrupt primary. On .bak recovery the .bak is renamed back to `path`.
[[nodiscard]] bool readAndParseWithBak(const char *path, JsonDocument &doc);

// One-line heap snapshot tag (compiled out on host builds).
void logLargestFreeBlock(const char *where);

// Decode "#RRGGBB" into the firmware-side color struct. Malformed input
// falls back to 0x000000.
void parseColor(const char *hex, CfgColor *out);

// Same as parseColor but returns the raw 0x00RRGGBB payload. Used by the
// color-ramp decoder which writes the value directly into the stop struct.
[[nodiscard]] uint32_t parseHexColorValue(const char *hex);

// Decode an even-length hex string into a byte buffer. Returns true on
// success and writes `*outLen`; false on any malformed input.
[[nodiscard]] bool decodeHexBytes(const char *hex, uint8_t *out, uint8_t maxLen, uint8_t *outLen);

// ---------------------------------------------------------------------------
// Validators (config_validators.cpp)
// ---------------------------------------------------------------------------

// Standard 11-bit CAN identifier max (#319). Anything above this requires the
// extended (29-bit) frame format.
inline constexpr uint32_t kCanStandardIdMax = 0x7FFu;
// 29-bit extended CAN identifier max — covers all valid TWAI extended IDs.
inline constexpr uint32_t kCanExtendedIdMax = 0x1FFFFFFFu;

// Compare a config file's "version" string against CONFIG_SCHEMA_VERSION.
// Logs + pushes to ErrorStore on mismatch but does not abort.
void checkSchemaVersion(const char *fileLabel, const char *fileVersion);

// String → enum parsers for the dashboard / signals / inputs schemas.
[[nodiscard]] TopBarItemKind parseTopBarItemKind(const char *str);
[[nodiscard]] TopBarItemPos parseTopBarItemPos(const char *str);
[[nodiscard]] CfgButtonActionType parseButtonActionType(const char *category, const char *type);
[[nodiscard]] CfgCruiseOp parseCruiseOp(const char *op);
[[nodiscard]] CfgArcFillStyle parseArcFillStyle(const char *str);
[[nodiscard]] WidgetType parseWidgetType(const char *str);
[[nodiscard]] CfgInputActive parseInputActive(const char *str);
[[nodiscard]] CfgInputPressKind parseInputPressKind(const char *str);

// Reject pins already claimed by the active TWAI config.
[[nodiscard]] bool isPinConflict(int8_t pin);

// ---------------------------------------------------------------------------
// Parsers (config_parser.cpp) — building blocks shared between loaders
// ---------------------------------------------------------------------------

// Parse a single button-action descriptor. Used by both dashboard buttons
// (parseButtonActionsArray) and input bindings (loadInputBindings).
void parseButtonAction(JsonObjectConst src, CfgButtonAction *out);

// Parse a single widget node from the dashboard.json `widgets[]` array.
// Dispatches on `type` and zero-fills `w` before populating type-specific
// fields. Driven by loadDashboard().
void parseWidget(JsonObjectConst src, CfgWidget *w);

// Parse the per-signal `colorRamp` block into the firmware-side struct.
// Tolerates missing fields; out->count==0 means "use default sensor lookup".
void parseColorRamp(JsonObjectConst src, const char *signalName, CfgColorRampDef *out);

} // namespace ConfigLoaderInternal
