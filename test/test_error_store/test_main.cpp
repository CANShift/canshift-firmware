#include "diag/error_store.h"

#include <stdio.h>
#include <string.h>
#include <unity.h>

namespace {

void resetStore() {
    ErrorStore::clear();
}

void pushCode(const char *code) {
    ErrorStore::push(ERROR_SRC_SYSTEM, code, "msg");
}

void assertOrder(const char *expected[], uint8_t expectedCount) {
    FwError buf[8];
    uint8_t got = 0;
    ErrorStore::getAll(buf, &got, 8);
    TEST_ASSERT_EQUAL_UINT8(expectedCount, got);
    for (uint8_t i = 0; i < expectedCount; i++) {
        TEST_ASSERT_EQUAL_STRING(expected[i], buf[i].code);
    }
}

void test_dismissAt_newest_matchesDismissLatest() {
    resetStore();
    pushCode("A");
    pushCode("B");
    pushCode("C");

    ErrorStore::dismissAt(0);

    const char *expected[] = {"B", "A"};
    assertOrder(expected, 2);
}

void test_dismissAt_oldest_dropsEnd() {
    resetStore();
    pushCode("A");
    pushCode("B");
    pushCode("C");

    ErrorStore::dismissAt(2);

    const char *expected[] = {"C", "B"};
    assertOrder(expected, 2);
}

void test_dismissAt_middle_collapsesGap() {
    resetStore();
    pushCode("A");
    pushCode("B");
    pushCode("C");
    pushCode("D");

    ErrorStore::dismissAt(1);

    const char *expected[] = {"D", "B", "A"};
    assertOrder(expected, 3);
}

void test_dismissAt_outOfRange_isNoOp() {
    resetStore();
    pushCode("A");
    pushCode("B");

    ErrorStore::dismissAt(5);

    const char *expected[] = {"B", "A"};
    assertOrder(expected, 2);
}

void test_dismissAt_emptyStore_isNoOp() {
    resetStore();
    ErrorStore::dismissAt(0);
    TEST_ASSERT_EQUAL_UINT8(0, ErrorStore::getCount());
}

void test_dismissAt_acrossRingWrap() {
    resetStore();
    pushCode("A");
    pushCode("B");
    pushCode("C");
    pushCode("D");
    pushCode("E");
    pushCode("F");
    pushCode("G");
    pushCode("H");

    ErrorStore::dismissAt(2);

    const char *expected[] = {"H", "G", "E", "D", "C"};
    assertOrder(expected, 5);
}

void test_dismissAt_versionAdvancesOnSuccess_notOnNoOp() {
    resetStore();
    pushCode("A");
    pushCode("B");

    const uint32_t before = ErrorStore::getVersion();
    ErrorStore::dismissAt(99);
    TEST_ASSERT_EQUAL_UINT32(before, ErrorStore::getVersion());

    ErrorStore::dismissAt(0);
    TEST_ASSERT_NOT_EQUAL_UINT32(before, ErrorStore::getVersion());
}

void test_push_codeAtFieldWidth_dedupesOnStoredKey() {
    resetStore();
    pushCode("CODE_AT_12CH");
    pushCode("CODE_AT_12CH");
    pushCode("CODE_AT_12CH");

    const char *expected[] = {"CODE_AT_12C"};
    assertOrder(expected, 1);
}

}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_dismissAt_newest_matchesDismissLatest);
    RUN_TEST(test_dismissAt_oldest_dropsEnd);
    RUN_TEST(test_dismissAt_middle_collapsesGap);
    RUN_TEST(test_dismissAt_outOfRange_isNoOp);
    RUN_TEST(test_dismissAt_emptyStore_isNoOp);
    RUN_TEST(test_dismissAt_acrossRingWrap);
    RUN_TEST(test_dismissAt_versionAdvancesOnSuccess_notOnNoOp);
    RUN_TEST(test_push_codeAtFieldWidth_dedupesOnStoredKey);
    return UNITY_END();
}
