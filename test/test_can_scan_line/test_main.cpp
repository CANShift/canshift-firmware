#include "hal/usb/can_scan_line.h"

#include <string.h>
#include <unity.h>

namespace {

constexpr size_t kLineCap = 96;
constexpr uint32_t kMaxId = 0xFFFFFFFFu;

} // namespace

void setUp() {}
void tearDown() {}

void test_aFrameFollowsTheWireShape() {
    char line[kLineCap];
    const uint8_t data[] = {1, 2, 255};
    const size_t len = CanScanLine::format(0x7E8, 3, data, line, sizeof(line));
    TEST_ASSERT_EQUAL_STRING("{\"can\":1,\"id\":2024,\"len\":3,\"d\":[1,2,255]}\n", line);
    TEST_ASSERT_EQUAL(strlen(line), len);
}

void test_anEmptyFrameHasAnEmptyDataArray() {
    char line[kLineCap];
    TEST_ASSERT_GREATER_THAN(0, CanScanLine::format(1, 0, nullptr, line, sizeof(line)));
    TEST_ASSERT_EQUAL_STRING("{\"can\":1,\"id\":1,\"len\":0,\"d\":[]}\n", line);
}

void test_anOverlongLengthIsClampedToEightBytes() {
    char line[kLineCap];
    const uint8_t data[16] = {9, 9, 9, 9, 9, 9, 9, 9, 7, 7, 7, 7, 7, 7, 7, 7};
    TEST_ASSERT_GREATER_THAN(0, CanScanLine::format(5, 200, data, line, sizeof(line)));
    TEST_ASSERT_EQUAL_STRING("{\"can\":1,\"id\":5,\"len\":8,\"d\":[9,9,9,9,9,9,9,9]}\n", line);
}

void test_theWorstCaseFrameFitsTheLineCap() {
    char line[kLineCap];
    const uint8_t data[8] = {255, 255, 255, 255, 255, 255, 255, 255};
    TEST_ASSERT_GREATER_THAN(0, CanScanLine::format(kMaxId, 8, data, line, sizeof(line)));
}

void test_aShortBufferFailsInsteadOfTruncating() {
    char line[24];
    const uint8_t data[8] = {255, 255, 255, 255, 255, 255, 255, 255};
    TEST_ASSERT_EQUAL(0, CanScanLine::format(kMaxId, 8, data, line, sizeof(line)));
}

void test_roomForTheTextButNotTheTerminatorFails() {
    const char *expected = "{\"can\":1,\"id\":1,\"len\":0,\"d\":[]}\n";
    char line[64];
    const size_t exact = strlen(expected);
    TEST_ASSERT_EQUAL(0, CanScanLine::format(1, 0, nullptr, line, exact));
    TEST_ASSERT_EQUAL(exact, CanScanLine::format(1, 0, nullptr, line, exact + 1));
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_aFrameFollowsTheWireShape);
    RUN_TEST(test_anEmptyFrameHasAnEmptyDataArray);
    RUN_TEST(test_anOverlongLengthIsClampedToEightBytes);
    RUN_TEST(test_theWorstCaseFrameFitsTheLineCap);
    RUN_TEST(test_aShortBufferFailsInsteadOfTruncating);
    RUN_TEST(test_roomForTheTextButNotTheTerminatorFails);
    return UNITY_END();
}
