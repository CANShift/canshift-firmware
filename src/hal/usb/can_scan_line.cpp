#include "can_scan_line.h"

#include "app_config.h"

#include <stdio.h>

namespace {

bool append(char *&p, const char *end, int written) {
    if (written <= 0 || p + written >= end)
        return false;
    p += written;
    return true;
}

bool appendBytes(char *&p, const char *end, uint8_t len, const uint8_t *data) {
    for (uint8_t i = 0; i < len; i++) {
        const int n = snprintf(p, static_cast<size_t>(end - p), i == 0 ? "%u" : ",%u",
                               static_cast<unsigned>(data[i]));
        if (!append(p, end, n))
            return false;
    }
    return true;
}

}

size_t CanScanLine::format(uint32_t id, uint8_t len, const uint8_t *data, char *out, size_t cap) {
    if (out == nullptr || cap == 0)
        return 0;
    const uint8_t bytes = len < kCanFrameMaxBytes ? len : kCanFrameMaxBytes;
    char *p = out;
    const char *end = out + cap;
    const int head = snprintf(p, cap, "{\"can\":1,\"id\":%lu,\"len\":%u,\"d\":[",
                              static_cast<unsigned long>(id), static_cast<unsigned>(bytes));
    if (!append(p, end, head) || !appendBytes(p, end, bytes, data))
        return 0;
    if (!append(p, end, snprintf(p, static_cast<size_t>(end - p), "]}\n")))
        return 0;
    return static_cast<size_t>(p - out);
}
