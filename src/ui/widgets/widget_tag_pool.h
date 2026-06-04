#pragma once
// widget_tag_pool.h — Shared fixed-size storage for widget Tag structs.
//
// Why this exists: every widget create() used to `new` its own Tag struct
// on the heap and `delete` it from a LV_EVENT_DELETE callback. That blows
// CLAUDE.md's "No dynamic allocation in hot paths" rule and fragments the
// shared heap over time.
//
// Earlier attempts used per-widget-type pools sized to
// CONFIG_MAX_WIDGETS_PER_PAGE — that gave ~8 widget × 12 slots × ~180 B
// each ≈ 8 KB of `.dram0.bss` because we sized each pool for the worst
// case of "all 12 widgets on a page are this type".
//
// A page is bounded to CONFIG_MAX_WIDGETS_PER_PAGE widgets *total*, not
// per type, and only the active page's widgets exist at any moment. So a
// single shared pool of CONFIG_MAX_WIDGETS_PER_PAGE slots — each sized to
// the largest tag — is enough. That collapses the BSS cost to one slot
// budget instead of eight.
//
// Threading: every allocation/release runs from the LVGL UI task under
// g_lvglMutex (widget create() and LV_EVENT_DELETE both fire there).
// No additional synchronisation needed.

#include <lvgl.h>
#include <new>
#include <stddef.h>
#include <stdint.h>

namespace WidgetTagPool {

// Pool capacity — must match CONFIG_MAX_WIDGETS_PER_PAGE (asserted in the
// .cpp). A single page can host this many widgets total, regardless of
// the mix between Bar / Gauge / Button / etc.
constexpr size_t kPoolSlots = 12;

// Slot size in bytes — must hold the largest Tag struct. Asserted at the
// `alloc<T>()` call site so any widget whose Tag grows past the budget
// fails to compile rather than silently truncating memory.
constexpr size_t kSlotBytes = 192;

// Slot alignment — covers the widest field any Tag uses (pointers,
// uint32_t, AlertFlash::State). 8 is generous on 32-bit Xtensa.
constexpr size_t kSlotAlign = 8;

// Raw slot reservation. Returns nullptr when all slots are busy — caller
// must surface a warning and bail out cleanly (delete the lv_obj that
// would have owned the tag).
void *allocRaw();

// Release a slot previously returned by allocRaw(). Caller is responsible
// for destruction (or, in practice, going through the template alloc/release
// helpers below which run the C++ destructor automatically).
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

// Generic LV_EVENT_DELETE handler — wire it up via
//     lv_obj_add_event_cb(obj, WidgetTagPool::deleteHandler<XxxTag>,
//                         LV_EVENT_DELETE, tag);
template <typename T>
void deleteHandler(lv_event_t *e) {
    auto *t = static_cast<T *>(lv_event_get_user_data(e));
    release(t);
}

// RAII guard around a single pool slot (#1207 firmware safety).
//
// Use case: between alloc<T>() and the moment the caller wires LVGL's
// LV_EVENT_DELETE callback (which becomes the deterministic cleanup path),
// any early return — exhausted heap, missing asset, parse failure — would
// leak the slot. `Slot<T>` closes that window:
//
//     WidgetTagPool::Slot<FooTag> slot;
//     FooTag *tag = slot.get();
//     if (!tag) { ... return nullptr; }     // pool exhausted, nothing to free
//     tag->foo = ...;
//     if (somethingFailed) return nullptr;  // dtor frees the slot
//     WidgetHelpers::attachTagDeleter(obj, slot.commit()); // LVGL now owns it
//
// Once `commit()` runs the guard disengages and the LV_EVENT_DELETE handler
// is responsible for `release(tag)` (typically via WidgetHelpers::attachTagDeleter).
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

    // Pointer into the slot, or nullptr when the pool was exhausted.
    T *get() const {
        return m_tag;
    }

    // Disengage the guard — caller (usually LVGL via LV_EVENT_DELETE) now
    // owns destruction. Returns the bare pointer for convenience.
    T *commit() {
        T *t = m_tag;
        m_tag = nullptr;
        return t;
    }

  private:
    T *m_tag;
};

} // namespace WidgetTagPool
