#include "json_reader.h"

#include <stdlib.h>

#ifdef ARDUINO
    #include <esp_heap_caps.h>
#endif

namespace JsonReader {

#ifdef ARDUINO
namespace {
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
}

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
}

ArduinoJson::Allocator *configAllocator() {
    static HeapAllocator allocator;
    return &allocator;
}
#endif

DeserializationError parse(JsonDocument &doc, const char *data, size_t len) {
    return deserializeJson(doc, data, len);
}

}
