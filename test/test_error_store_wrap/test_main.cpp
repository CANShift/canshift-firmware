
#include "diag/error_store.h"

#include <stdio.h>
#include <string.h>
#include <unity.h>

namespace {

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

}

void setUp() {
    ErrorStore::clear();
}

void tearDown() {}

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

void test_push_oneOverCapacity_dropsOldest() {
    pushCode("A");
    pushCode("B");
    pushCode("C");
    pushCode("D");
    pushCode("E");
    pushCode("F");
    pushCode("G");

    TEST_ASSERT_EQUAL_UINT8(kRingSize, ErrorStore::getCount());
    const char *expected[] = {"G", "F", "E", "D", "C", "B"};
    assertOrder(expected, kRingSize);
}

void test_push_manyOverCapacity_keepsNewestSix() {
    const char *codes[] = {"A", "B", "C", "D", "E", "F", "G", "H", "I", "J"};
    for (const char *c : codes) {
        pushCode(c);
    }

    TEST_ASSERT_EQUAL_UINT8(kRingSize, ErrorStore::getCount());
    const char *expected[] = {"J", "I", "H", "G", "F", "E"};
    assertOrder(expected, kRingSize);
}

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

void test_push_duplicateKey_doesNotEvict() {
    pushCode("A");
    pushCode("B");
    pushCode("C");
    pushCode("D");
    pushCode("E");
    pushCode("F");

    for (int i = 0; i < 100; ++i) {
        ErrorStore::push(ERROR_SRC_SYSTEM, "F", "updated");
    }

    TEST_ASSERT_EQUAL_UINT8(kRingSize, ErrorStore::getCount());
    const char *expected[] = {"F", "E", "D", "C", "B", "A"};
    assertOrder(expected, kRingSize);

    FwError buf[kRingSize];
    uint8_t got = 0;
    ErrorStore::getAll(buf, &got, kRingSize);
    TEST_ASSERT_EQUAL_STRING("updated", buf[0].message);
}

void test_dismissLatest_afterWrap_popsNewest() {
    pushCode("A");
    pushCode("B");
    pushCode("C");
    pushCode("D");
    pushCode("E");
    pushCode("F");
    pushCode("G");
    pushCode("H");

    ErrorStore::dismissLatest();

    TEST_ASSERT_EQUAL_UINT8(kRingSize - 1, ErrorStore::getCount());
    const char *expected[] = {"G", "F", "E", "D", "C"};
    assertOrder(expected, kRingSize - 1);
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_push_fillsToCapacity_keepsAll);
    RUN_TEST(test_push_oneOverCapacity_dropsOldest);
    RUN_TEST(test_push_manyOverCapacity_keepsNewestSix);
    RUN_TEST(test_push_overCapacity_versionAdvances);
    RUN_TEST(test_push_duplicateKey_doesNotEvict);
    RUN_TEST(test_dismissLatest_afterWrap_popsNewest);
    return UNITY_END();
}
