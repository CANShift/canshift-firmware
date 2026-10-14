#pragma once

#include <stddef.h>
#include <stdint.h>

namespace UsbStatusLine {

struct Fields {
    const char *version;
    unsigned protocol;
    bool isDay;
    const char *boardId;
};

[[nodiscard]] size_t format(const Fields &fields, char *out, size_t cap);

} // namespace UsbStatusLine
