// test_main.cpp — Ring-buffer wrap behaviour for ErrorStore (#912).
//
// The base suite in test_error_store/ exercises dismissAt's per-row semantics.
// This suite locks the wrap-around contract:
//   - Capacity is RING_SIZE (6) — push #7 evicts the oldest, push #8 evicts
//     the next-oldest, etc.
//   - getCount stays at RING_SIZE once full — never grows past capacity.
//   - getAll's newest-first order keeps holding after wrap (the most recent
//     RING_SIZE pushes are visible, oldest first dropped).
//   - Duplicate source+code updates in place — pushing the same key past
//     capacity does NOT evict anything; the ring stays the same shape.

#include "diag/error_store.h"

#include <stdio.h>
#include <string.h>
#include <unity.h>

namespace {

// Mirrors RING_SIZE in src/diag/error_store.cpp. Kept as a local constant so
// a change there forces a deliberate update here — a silent capacity bump
// would otherwise quietly invalidate the wrap assertions below.
constexpr uint8_t kRingSize = 6;

void pushCode(const char *code) {
    ErrorStore::push(ERROR_SRC_SYSTEM, code, "msg");
}

void assertOrder(const char *expected[], uint8_t expectedCount) {
    FwError buf[kRingSize];
    uint8_t got = 0;
    ErrorStore::getAll(buf, &got, kRingSize);
    TEST_ASSERT_EQUAL_UINT8(expectedCount, got);
    for (uint8_t i = 0; i < expectedCount; i++) {
        TEST_ASSERT_EQUAL_STRING(expected[i], buf[i].code);
    }
}

} // namespace

void setUp() {
    ErrorStore::clear();
}

void tearDown() {}

// Filling exactly to capacity does not wrap — every push is visible.
void test_push_fillsToCapacity_keepsAll() {
    pushCode("A");
    pushCode("B");
    pushCode("C");
    pushCode("D");
    pushCode("E");
    pushCode("F");

    TEST_ASSERT_EQUAL_UINT8(kRingSize, ErrorStore::getCount());
    const char *expected[] = {"F", "E", "D", "C", "B", "A"};
    assertOrder(expected, kRingSize);
}

// One push past capacity evicts the oldest entry. Count stays at RING_SIZE.
// This is the headline behaviour flagged in #912.
void test_push_oneOverCapacity_dropsOldest() {
    pushCode("A");
    pushCode("B");
    pushCode("C");
    pushCode("D");
    pushCode("E");
    pushCode("F");
    pushCode("G"); // ring full → "A" evicted

    TEST_ASSERT_EQUAL_UINT8(kRingSize, ErrorStore::getCount());
    const char *expected[] = {"G", "F", "E", "D", "C", "B"};
    assertOrder(expected, kRingSize);
}

// Multiple pushes past capacity continue evicting oldest-first. Locks the
// invariant that the ring holds the most recent RING_SIZE entries.
void test_push_manyOverCapacity_keepsNewestSix() {
    const char *codes[] = {"A", "B", "C", "D", "E", "F", "G", "H", "I", "J"};
    for (const char *c : codes) {
        pushCode(c);
    }

    TEST_ASSERT_EQUAL_UINT8(kRingSize, ErrorStore::getCount());
    const char *expected[] = {"J", "I", "H", "G", "F", "E"};
    assertOrder(expected, kRingSize);
}

// Each push past capacity bumps version — clients polling getVersion() must
// see a change so the UI repaints even though count stayed pinned at RING_SIZE.
void test_push_overCapacity_versionAdvances() {
    pushCode("A");
    pushCode("B");
    pushCode("C");
    pushCode("D");
    pushCode("E");
    pushCode("F");
    const uint32_t versionFull = ErrorStore::getVersion();

    pushCode("G");
    const uint32_t versionAfterWrap = ErrorStore::getVersion();
    TEST_ASSERT_NOT_EQUAL_UINT32(versionFull, versionAfterWrap);

    pushCode("H");
    TEST_ASSERT_NOT_EQUAL_UINT32(versionAfterWrap, ErrorStore::getVersion());
}

// Duplicate source+code updates in place — push() short-circuits before the
// eviction path. Pushing the same key 100 times past a full ring must NOT
// drop the other five entries.
void test_push_duplicateKey_doesNotEvict() {
    pushCode("A");
    pushCode("B");
    pushCode("C");
    pushCode("D");
    pushCode("E");
    pushCode("F"); // ring full

    for (int i = 0; i < 100; ++i) {
        ErrorStore::push(ERROR_SRC_SYSTEM, "F", "updated");
    }

    TEST_ASSERT_EQUAL_UINT8(kRingSize, ErrorStore::getCount());
    const char *expected[] = {"F", "E", "D", "C", "B", "A"};
    assertOrder(expected, kRingSize);

    // And the in-place update actually took effect on the duplicate slot.
    FwError buf[kRingSize];
    uint8_t got = 0;
    ErrorStore::getAll(buf, &got, kRingSize);
    TEST_ASSERT_EQUAL_STRING("updated", buf[0].message);
}

// After full-ring wrap, dismissLatest still pops the newest and leaves the
// remaining wrap-window intact. Catches a regression where the head/tail
// pointers desync after wrap.
void test_dismissLatest_afterWrap_popsNewest() {
    pushCode("A");
    pushCode("B");
    pushCode("C");
    pushCode("D");
    pushCode("E");
    pushCode("F");
    pushCode("G");
    pushCode("H"); // ring: {H, G, F, E, D, C} newest-first

    ErrorStore::dismissLatest(); // → drops H

    TEST_ASSERT_EQUAL_UINT8(kRingSize - 1, ErrorStore::getCount());
    const char *expected[] = {"G", "F", "E", "D", "C"};
    assertOrder(expected, kRingSize - 1);
}

int main(int /*argc*/, char ** /*argv*/) {
    UNITY_BEGIN();
    RUN_TEST(test_push_fillsToCapacity_keepsAll);
    RUN_TEST(test_push_oneOverCapacity_dropsOldest);
    RUN_TEST(test_push_manyOverCapacity_keepsNewestSix);
    RUN_TEST(test_push_overCapacity_versionAdvances);
    RUN_TEST(test_push_duplicateKey_doesNotEvict);
    RUN_TEST(test_dismissLatest_afterWrap_popsNewest);
    return UNITY_END();
}
