#pragma once

#include "config/config_types.h"
#include "ui/widgets/timer_sources.h"

#include <stddef.h>

namespace StatusFieldText {

inline constexpr char kStalePlaceholder[] = "- -";

void formatValue(char *buf, size_t cap, float value, const char *spec, const CfgTopBarItem &item);

void withPrefix(char *buf, size_t cap, const char *prefix, const char *value);

[[nodiscard]] bool formatTimer(char *buf, size_t cap, const CfgTopBarItem &item,
                               const TimerSources::Inputs &in);

const char *flagWord(bool valid, float value, const CfgTopBarItem &item);

}
