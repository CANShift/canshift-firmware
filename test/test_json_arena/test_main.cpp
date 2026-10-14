#include "hal/ble/ble_json_budget.h"
#include "util/json_arena.h"

#include <stdio.h>
#include <string.h>
#include <string>
#include <unity.h>

namespace {

constexpr size_t kProbeBytes = 64 * 1024;
constexpr size_t kTinyArenaBytes = 64;
constexpr char kBoardId[] = "waveshare_s3_28_board_profile__";

std::string longestArray() {
    std::string text = "[";
    while (text.size() + 3 < BleJsonBudget::kMaxWriteBytes) {
        text += "0,";
    }
    return text + "0]";
}

std::string longestObject() {
    std::string text = "{";
    char member[16];
    for (int i = 0; text.size() + 16 < BleJsonBudget::kMaxWriteBytes; ++i) {
        snprintf(member, sizeof(member), "\"k%03d\":%d,", i, i);
        text += member;
    }
    return text + "\"z\":0}";
}

std::string longestString() {
    return "{\"k\":\"" + std::string(BleJsonBudget::kMaxWriteBytes - 10, 'x') + "\"}";
}

std::string manyStrings() {
    std::string text = "[";
    char member[16];
    for (int i = 0; text.size() + 10 < BleJsonBudget::kMaxWriteBytes; ++i) {
        snprintf(member, sizeof(member), "\"s%04d\",", i);
        text += member;
    }
    return text + "\"\"]";
}

void fillStatus(JsonDocument &doc) {
    doc["ver"] = "0.20.1";
    doc["board_id"] = kBoardId;
    doc["can"] = 1;
    doc["is_day"] = 0;
}

std::string heapSerialized(const std::string &input) {
    JsonDocument doc;
    TEST_ASSERT_FALSE(deserializeJson(doc, input.data(), input.size()));
    std::string out;
    serializeJson(doc, out);
    return out;
}

void assertWriteFitsCallbackArena(const std::string &input) {
    TEST_ASSERT_LESS_OR_EQUAL_UINT32(BleJsonBudget::kMaxWriteBytes, input.size());
    StaticJsonArena<BleJsonBudget::kCallbackArenaBytes> arena;
    {
        JsonDocument doc(&arena);
        TEST_ASSERT_FALSE(deserializeJson(doc, input.data(), input.size()));
        std::string out;
        serializeJson(doc, out);
        TEST_ASSERT_EQUAL_STRING(heapSerialized(input).c_str(), out.c_str());
    }
    TEST_ASSERT_EQUAL_UINT32(0, arena.liveBlocks());
}

} // namespace

void setUp() {}

void tearDown() {}

void test_largestBleWritesFitTheCallbackArena() {
    assertWriteFitsCallbackArena(longestArray());
    assertWriteFitsCallbackArena(longestObject());
    assertWriteFitsCallbackArena(longestString());
    assertWriteFitsCallbackArena(manyStrings());
    assertWriteFitsCallbackArena("[[[[[[[[[[0]]]]]]]]]]");
}

void test_statusDocumentFitsItsArenaAndMatchesTheHeap() {
    StaticJsonArena<BleJsonBudget::kStatusArenaBytes> arena;
    JsonDocument arenaDoc(&arena);
    JsonDocument heapDoc;
    fillStatus(arenaDoc);
    fillStatus(heapDoc);
    TEST_ASSERT_FALSE(arenaDoc.overflowed());
    std::string fromArena;
    std::string fromHeap;
    serializeJson(arenaDoc, fromArena);
    serializeJson(heapDoc, fromHeap);
    TEST_ASSERT_EQUAL_STRING(fromHeap.c_str(), fromArena.c_str());
}

void test_arenaIsEmptyAgainAfterEachDocument() {
    StaticJsonArena<BleJsonBudget::kCallbackArenaBytes> arena;
    for (int round = 0; round < 100; ++round) {
        JsonDocument doc(&arena);
        const std::string input = round % 2 ? longestObject() : longestString();
        TEST_ASSERT_FALSE(deserializeJson(doc, input.data(), input.size()));
        doc.clear();
        TEST_ASSERT_EQUAL_UINT32(0, arena.liveBlocks());
    }
}

void test_exhaustedArenaReportsNoMemoryInsteadOfCorrupting() {
    StaticJsonArena<kTinyArenaBytes> arena;
    JsonDocument doc(&arena);
    const std::string input = longestObject();
    TEST_ASSERT_EQUAL(DeserializationError::NoMemory,
                      deserializeJson(doc, input.data(), input.size()).code());
    fillStatus(doc);
    TEST_ASSERT_TRUE(doc.overflowed());
}

void test_reallocateKeepsContentsWhenMoved() {
    StaticJsonArena<kProbeBytes> arena;
    char *first = static_cast<char *>(arena.allocate(8));
    memcpy(first, "abcdefg", 8);
    void *pinned = arena.allocate(16);
    char *grown = static_cast<char *>(arena.reallocate(first, 64));
    TEST_ASSERT_NOT_NULL(grown);
    TEST_ASSERT_EQUAL_STRING("abcdefg", grown);
    arena.deallocate(pinned);
    arena.deallocate(grown);
    TEST_ASSERT_EQUAL_UINT32(0, arena.liveBlocks());
}

void test_failedGrowLeavesTheBlockIntact() {
    StaticJsonArena<kTinyArenaBytes> arena;
    char *block = static_cast<char *>(arena.allocate(8));
    memcpy(block, "keepme!", 8);
    TEST_ASSERT_NULL(arena.reallocate(block, kTinyArenaBytes * 2));
    void *after = arena.allocate(8);
    TEST_ASSERT_NOT_NULL(after);
    TEST_ASSERT_EQUAL_STRING("keepme!", block);
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_largestBleWritesFitTheCallbackArena);
    RUN_TEST(test_statusDocumentFitsItsArenaAndMatchesTheHeap);
    RUN_TEST(test_arenaIsEmptyAgainAfterEachDocument);
    RUN_TEST(test_exhaustedArenaReportsNoMemoryInsteadOfCorrupting);
    RUN_TEST(test_reallocateKeepsContentsWhenMoved);
    RUN_TEST(test_failedGrowLeavesTheBlockIntact);
    return UNITY_END();
}
