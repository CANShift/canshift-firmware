#include "json_arena.h"

#include <string.h>

namespace {

constexpr size_t kAlignment = alignof(max_align_t);

constexpr size_t alignUp(size_t value) {
    return (value + kAlignment - 1) & ~(kAlignment - 1);
}

constexpr size_t kHeaderBytes = alignUp(sizeof(size_t) * 2);

} // namespace

JsonArena::Header *JsonArena::headerOf(void *block) const {
    return reinterpret_cast<Header *>(static_cast<uint8_t *>(block) - kHeaderBytes);
}

bool JsonArena::isTop(const Header *header) const {
    const uint8_t *end =
        reinterpret_cast<const uint8_t *>(header) + kHeaderBytes + alignUp(header->size);
    return end == storage_ + top_;
}

void *JsonArena::carve(size_t size) {
    const size_t needed = kHeaderBytes + alignUp(size);
    if (needed > capacity_ - top_) {
        return nullptr;
    }
    Header *header = reinterpret_cast<Header *>(storage_ + top_);
    header->size = size;
    header->previousTop = top_;
    top_ += needed;
    peak_ = top_ > peak_ ? top_ : peak_;
    return reinterpret_cast<uint8_t *>(header) + kHeaderBytes;
}

void *JsonArena::growTop(Header *header, size_t size) {
    const size_t top = top_;
    top_ = header->previousTop;
    void *block = carve(size);
    top_ = block != nullptr ? top_ : top;
    return block;
}

void *JsonArena::allocate(size_t size) {
    void *block = carve(size);
    live_ += block != nullptr ? 1 : 0;
    return block;
}

void JsonArena::deallocate(void *block) {
    if (block == nullptr) {
        return;
    }
    const Header *header = headerOf(block);
    if (isTop(header)) {
        top_ = header->previousTop;
    }
    --live_;
    top_ = live_ == 0 ? 0 : top_;
}

void *JsonArena::reallocate(void *block, size_t size) {
    if (block == nullptr) {
        return allocate(size);
    }
    Header *header = headerOf(block);
    if (isTop(header)) {
        return growTop(header, size);
    }
    void *moved = carve(size);
    if (moved == nullptr) {
        return nullptr;
    }
    memcpy(moved, block, header->size < size ? header->size : size);
    return moved;
}
