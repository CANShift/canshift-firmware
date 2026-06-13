#pragma once

#include <lvgl.h>
#include <new>
#include <stddef.h>
#include <stdint.h>

namespace WidgetTagPool {

constexpr size_t kPoolSlots = 12;

constexpr size_t kSlotBytes = 192;

constexpr size_t kSlotAlign = 8;

void *allocRaw();

void releaseRaw(void *p);

template <typename T>
T *alloc() {
    static_assert(sizeof(T) <= kSlotBytes, "widget Tag exceeds kSlotBytes — bump it");
    static_assert(alignof(T) <= kSlotAlign, "widget Tag alignment exceeds kSlotAlign");
    void *raw = allocRaw();
    return raw ? new (raw) T() : nullptr;
}

template <typename T>
void release(T *tag) {
    if (!tag)
        return;
    tag->~T();
    releaseRaw(static_cast<void *>(tag));
}

template <typename T>
void deleteHandler(lv_event_t *e) {
    auto *t = static_cast<T *>(lv_event_get_user_data(e));
    release(t);
}

template <typename T>
class Slot {
  public:
    Slot() : m_tag(alloc<T>()) {}
    ~Slot() {
        if (m_tag) {
            release(m_tag);
        }
    }

    Slot(const Slot &) = delete;
    Slot &operator=(const Slot &) = delete;
    Slot(Slot &&) = delete;
    Slot &operator=(Slot &&) = delete;

    T *get() const {
        return m_tag;
    }

    T *commit() {
        T *t = m_tag;
        m_tag = nullptr;
        return t;
    }

  private:
    T *m_tag;
};

} // namespace WidgetTagPool
