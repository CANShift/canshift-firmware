#pragma once

#include "config_types.h"

#include <ArduinoJson.h>
#include <cstddef>
#include <cstdint>

namespace ConfigLoaderInternal {

extern CfgDashboard *s_dashboard;
extern CfgSignalConfig s_signals;
extern CfgDeviceConfig s_device;
extern CfgInputBindings s_inputs;

[[nodiscard]] uint8_t *acquireRollbackSnapshot();

[[nodiscard]] bool loadDashboard();
[[nodiscard]] bool loadSignals();
[[nodiscard]] bool loadDevice();
[[nodiscard]] bool loadInputBindings();

[[nodiscard]] bool readAndParseWithBak(const char *path, JsonDocument &doc);
const char *lastParseError();

void logLargestFreeBlock(const char *where);

void parseColor(const char *hex, CfgColor *out);

[[nodiscard]] uint32_t parseHexColorValue(const char *hex);

[[nodiscard]] bool decodeHexBytes(const char *hex, uint8_t *out, uint8_t maxLen, uint8_t *outLen);

inline constexpr uint32_t kCanStandardIdMax = 0x7FFu;

inline constexpr uint32_t kCanExtendedIdMax = 0x1FFFFFFFu;

void checkSchemaVersion(const char *fileLabel, const char *fileVersion);

[[nodiscard]] TopBarItemKind parseTopBarItemKind(const char *str);
[[nodiscard]] TopBarItemPos parseTopBarItemPos(const char *str);
[[nodiscard]] CfgButtonActionType parseButtonActionType(const char *category, const char *type);
[[nodiscard]] CfgCruiseOp parseCruiseOp(const char *op);
[[nodiscard]] CfgArcFillStyle parseArcFillStyle(const char *str);
[[nodiscard]] WidgetType parseWidgetType(const char *str);
[[nodiscard]] CfgInputActive parseInputActive(const char *str);
[[nodiscard]] CfgInputPressKind parseInputPressKind(const char *str);

[[nodiscard]] bool isPinConflict(int8_t pin);

void parseButtonAction(JsonObjectConst src, CfgButtonAction *out);

void parseWidget(JsonObjectConst src, CfgWidget *w);

void parseColorRamp(JsonObjectConst src, const char *signalName, CfgColorRampDef *out);

} // namespace ConfigLoaderInternal
