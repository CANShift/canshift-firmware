#pragma once

#include <ArduinoJson.h>
#include <stddef.h>
#include <stdint.h>

class JsonArena : public ArduinoJson::Allocator {
  public:
    JsonArena(uint8_t *storage, size_t capacity) : storage_(storage), capacity_(capacity) {}
    virtual ~JsonArena() = default;

    JsonArena(const JsonArena &) = delete;
    JsonArena &operator=(const JsonArena &) = delete;

    void *allocate(size_t size) override;
    void deallocate(void *block) override;
    void *reallocate(void *block, size_t size) override;

    size_t peak() const {
        return peak_;
    }
    size_t liveBlocks() const {
        return live_;
    }

  private:
    struct Header {
        size_t size;
        size_t previousTop;
    };

    Header *headerOf(void *block) const;
    bool isTop(const Header *header) const;
    void *carve(size_t size);
    void *growTop(Header *header, size_t size);

    uint8_t *storage_;
    size_t capacity_;
    size_t top_ = 0;
    size_t peak_ = 0;
    size_t live_ = 0;
};

template <size_t Capacity>
class StaticJsonArena : public JsonArena {
  public:
    StaticJsonArena() : JsonArena(bytes_, Capacity) {}

  private:
    alignas(alignof(max_align_t)) uint8_t bytes_[Capacity];
};
