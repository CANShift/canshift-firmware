#include "heap_stats.h"

#include "hal/usb/usb_comm.h"
#include "logger.h"

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <stdio.h>

namespace HeapStats {

namespace {

constexpr uint32_t EMIT_INTERVAL_MS = 30 * 1000;
constexpr size_t FRAME_BUF_SIZE = 160;

Snapshot s_latest = {};
uint32_t s_lastEmitMs = 0;
bool s_warmedUp = false;

bool s_psramProbed = false;
bool s_hasPsram = false;

bool hasPsram() {
    if (!s_psramProbed) {
        s_hasPsram = heap_caps_get_total_size(MALLOC_CAP_SPIRAM) > 0;
        s_psramProbed = true;
    }
    return s_hasPsram;
}

Snapshot sampleInternal() {
    Snapshot s = {};
    s.tsMs = millis();
    s.freeInternal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    s.largestInternal = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    s.hasPsram = hasPsram();
    if (s.hasPsram) {
        s.freePsram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
        s.largestPsram = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
    }
    return s;
}

void emitFrame(const Snapshot &s) {
    char buf[FRAME_BUF_SIZE];
    int n;
    if (s.hasPsram) {
        n = snprintf(buf, sizeof(buf),
                     "{\"heap_stats\":1,\"ts\":%lu,\"free_int\":%lu,\"largest_int\":%lu,"
                     "\"free_psram\":%lu,\"largest_psram\":%lu}",
                     static_cast<unsigned long>(s.tsMs), static_cast<unsigned long>(s.freeInternal),
                     static_cast<unsigned long>(s.largestInternal),
                     static_cast<unsigned long>(s.freePsram),
                     static_cast<unsigned long>(s.largestPsram));
    } else {
        n = snprintf(buf, sizeof(buf),
                     "{\"heap_stats\":1,\"ts\":%lu,\"free_int\":%lu,\"largest_int\":%lu,"
                     "\"free_psram\":null,\"largest_psram\":null}",
                     static_cast<unsigned long>(s.tsMs), static_cast<unsigned long>(s.freeInternal),
                     static_cast<unsigned long>(s.largestInternal));
    }
    if (n <= 0 || static_cast<size_t>(n) >= sizeof(buf)) {
        LOG_WARN("HEAP", "heap_stats payload truncated (n=%d, cap=%u)", n,
                 static_cast<unsigned>(sizeof(buf)));
        return;
    }
    UsbComm::sendLine(buf);
}

} // namespace

Snapshot sampleNow() {
    s_latest = sampleInternal();
    return s_latest;
}

const Snapshot &latest() {
    return s_latest;
}

void tick() {
    const uint32_t now = millis();
    if (s_warmedUp && (now - s_lastEmitMs) < EMIT_INTERVAL_MS) {
        return;
    }
    s_warmedUp = true;
    s_latest = sampleInternal();
    s_lastEmitMs = now;
    emitFrame(s_latest);
}

void emitNow() {
    s_latest = sampleInternal();
    s_lastEmitMs = millis();
    s_warmedUp = true;
    emitFrame(s_latest);
}

} // namespace HeapStats
