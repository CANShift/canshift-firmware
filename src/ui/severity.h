#pragma once

#include "alert_engine_rs.h"

#include <cmath>
#include <lvgl.h>
#include <stdint.h>

namespace Severity {

enum class Level : uint8_t {
    INFORMATION = kAlertSeverityInformation,
    WARNING = kAlertSeverityWarning,
    CRITICAL = kAlertSeverityCritical,
    FAILURE = kAlertSeverityFailure,
};

constexpr uint8_t kLevelCount = kAlertSeverityLevelCount;

constexpr uint8_t kRulePrimaryPx = 2;
constexpr uint8_t kRuleSecondaryPx = 1;
constexpr uint8_t kKickerPx = 10;
constexpr uint8_t kReasonPx = 10;
constexpr int16_t kKickerTrackingPx = 2;
constexpr int16_t kRuleGapPx = 3;
constexpr int16_t kRightInsetPx = 8;

[[nodiscard]] Level fromRaw(uint8_t raw);

[[nodiscard]] Level strongerOf(Level a, Level b);

[[nodiscard]] Level forReading(float value, float warnLevel, float dangerLevel, bool dangerBelow);

struct SignalLimits {
    float warn = NAN;
    float danger = NAN;
    float highWarn = NAN;
    float highDanger = NAN;
};

[[nodiscard]] Level forSignal(float value, const SignalLimits &limits);

[[nodiscard]] uint32_t inkFor(Level level);

[[nodiscard]] uint32_t kickerRgbFor(Level level);

[[nodiscard]] uint32_t baseRuleRgbFor(uint8_t rulePx, uint32_t baseInkRgb);

[[nodiscard]] bool floodsGround(Level level);

[[nodiscard]] int16_t kickerGapFor(Level level);

struct Surface {
    lv_obj_t *root = nullptr;
    lv_obj_t *rule = nullptr;
    lv_obj_t *kicker = nullptr;
    lv_obj_t *value = nullptr;
    uint8_t rulePx = kRulePrimaryPx;
    uint32_t baseInkRgb = 0;
    uint32_t baseRuleRgb = 0;
    Level level = Level::INFORMATION;
    bool painted = false;
};

struct Spec {
    Level level;
    const char *kicker;
    uint8_t rulePx;
};

[[nodiscard]] Surface build(lv_obj_t *parent, const Spec &spec);

[[nodiscard]] Surface adopt(lv_obj_t *rule, lv_obj_t *kicker, lv_obj_t *value, uint8_t rulePx,
                            uint32_t baseInkRgb);

[[nodiscard]] lv_obj_t *addReason(const Surface &surface, const char *text);

void setKicker(const Surface &surface, const char *text);

void repaint(Surface &surface, Level level);

} // namespace Severity
