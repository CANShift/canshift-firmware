#include "json_reader.h"

#include <stdlib.h>

#ifdef ARDUINO
    #include <esp_heap_caps.h>
#endif

namespace JsonReader {

#ifdef ARDUINO
namespace {
// Large configs (35 KB dashboard = ~3x that in parsed nodes) exhaust the
// internal heap alongside BLE + LVGL; PSRAM absorbs them, with an internal
// fallback so boards without PSRAM keep the old behavior.
struct PsramAllocator : ArduinoJson::Allocator {
    void *allocate(size_t size) override {
        void *p = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        return p ? p : malloc(size);
    }
    void deallocate(void *ptr) override {
        free(ptr);
    }
    void *reallocate(void *ptr, size_t newSize) override {
        void *p = heap_caps_realloc(ptr, newSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        return p ? p : realloc(ptr, newSize);
    }
};
} // namespace

ArduinoJson::Allocator *configAllocator() {
    static PsramAllocator allocator;
    return &allocator;
}
#else
namespace {
struct HeapAllocator : ArduinoJson::Allocator {
    void *allocate(size_t size) override {
        return malloc(size);
    }
    void deallocate(void *ptr) override {
        free(ptr);
    }
    void *reallocate(void *ptr, size_t newSize) override {
        return realloc(ptr, newSize);
    }
};
} // namespace

ArduinoJson::Allocator *configAllocator() {
    static HeapAllocator allocator;
    return &allocator;
}
#endif

DeserializationError parse(JsonDocument &doc, const char *data, size_t len) {
    return deserializeJson(doc, data, len);
}

DeserializationError parseFiltered(JsonDocument &doc, const char *data, size_t len,
                                   JsonDocument &filter) {
    return deserializeJson(doc, data, len, DeserializationOption::Filter(filter));
}

} // namespace JsonReader
