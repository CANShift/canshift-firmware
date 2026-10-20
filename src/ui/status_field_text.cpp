#include "ui/status_field_text.h"

#include "ui/unit_display.h"
#include "util/format_float.h"

#include <stdio.h>
#include <string.h>

namespace StatusFieldText {

void formatValue(char *buf, size_t cap, float value, const char *spec, const CfgTopBarItem &item) {
    const float shown = UnitDisplay::valueFor(value, item.unit);
    FloatFormat::formatFromSpec(buf, cap, shown, spec);
    if (!item.showUnit || item.unit[0] == '\0')
        return;
    const size_t used = strlen(buf);
    snprintf(buf + used, cap - used, " %s", UnitDisplay::symbolFor(item.unit));
}

void withPrefix(char *buf, size_t cap, const char *prefix, const char *value) {
    if (prefix[0] == '\0') {
        strlcpy(buf, value, cap);
        return;
    }
    snprintf(buf, cap, "%s %s", prefix, value);
}

bool formatTimer(char *buf, size_t cap, const CfgTopBarItem &item, const TimerSources::Inputs &in) {
    char value[TimerSources::kTextCapacity];
    const bool known = TimerSources::render(item.timerSource, in, value, sizeof(value));
    withPrefix(buf, cap, item.text, value);
    return known;
}

const char *flagWord(bool valid, float value, const CfgTopBarItem &item) {
    if (!valid)
        return kStalePlaceholder;
    return value != 0.0f ? item.text : item.offText;
}

}
