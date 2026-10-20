#include "app_config.h"
#if APP_SD_DATALOG_ENABLED

    #include "datalog_shared.h"

    #include "datalog_rs.h"
    #include "diag/logger.h"

    #include <atomic>
    #include <esp_heap_caps.h>

namespace DatalogShared {

namespace {

DatalogRingRs s_ring = {};
bool s_reserved = false;
std::atomic<bool> s_dropWarned{false};
std::atomic<bool> s_recording{false};
std::atomic<uint32_t> s_startMs{0};

}

bool reserve() {
    if (s_reserved)
        return true;
    auto *storage = static_cast<uint8_t *>(heap_caps_malloc(kDatalogRingBytes, MALLOC_CAP_SPIRAM));
    if (storage == nullptr) {
        LOG_ERROR("DLOG", "no PSRAM for the %u B datalog ring",
                  static_cast<unsigned>(kDatalogRingBytes));
        return false;
    }
    s_reserved = datalog_ring_init_rs(&s_ring, storage, kDatalogRingBytes);
    if (!s_reserved) {
        LOG_ERROR("DLOG", "datalog ring refused its storage");
        heap_caps_free(storage);
    }
    return s_reserved;
}

void enqueue(const uint8_t *record, size_t len) {
    if (len == 0 || datalog_ring_push_rs(&s_ring, record, len))
        return;
    if (s_dropWarned.exchange(true))
        return;
    LOG_WARN("DLOG", "datalog ring full, dropping records until the writer catches up");
}

size_t take(uint8_t *out, size_t capacity) {
    return datalog_ring_pop_rs(&s_ring, out, capacity);
}

uint32_t dropped() {
    return datalog_ring_dropped_rs(&s_ring);
}

void beginRecording(uint32_t startMs) {
    s_startMs.store(startMs);
    s_dropWarned.store(false);
    s_recording.store(true);
}

void endRecording() {
    s_recording.store(false);
}

bool recording() {
    return s_recording.load();
}

uint32_t startMs() {
    return s_startMs.load();
}

}

#endif
