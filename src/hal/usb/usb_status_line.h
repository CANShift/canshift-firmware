#pragma once

#include <ArduinoJson.h>
#include <stddef.h>
#include <stdint.h>

namespace UsbStatusLine {

struct Fields {
    const char *version;
    unsigned protocol;
    bool isDay;
    const char *boardId;
    void (*appendSound)(JsonObject out);
};

[[nodiscard]] size_t format(const Fields &fields, char *out, size_t cap);

}
