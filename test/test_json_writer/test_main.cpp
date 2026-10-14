#include "util/json_writer.h"

#include <string.h>
#include <string>
#include <unity.h>
#include <vector>

namespace {

constexpr size_t kGuardBytes = 4;
constexpr char kGuard = '#';

void fillSample(JsonDocument &doc) {
    doc["status"] = "ok";
    doc["ver"] = "0.20.1";
    doc["rpm"] = 7250;
    doc["neg"] = -42;
    doc["ratio"] = 0.1f;
    doc["pi"] = 3.14159265358979;
    doc["tiny"] = 1.5e-7;
    doc["big"] = 3.4e38f;
    doc["flag"] = true;
    doc["none"] = nullptr;
    doc["escaped"] = "quote \" slash \\ tab \t newline \n";
    doc["utf8"] = "\xC3\xA9t\xC3\xA9 \xE2\x82\xAC";
    JsonArray pages = doc["pages"].to<JsonArray>();
    for (int i = 0; i < 3; ++i) {
        JsonObject page = pages.add<JsonObject>();
        page["id"] = i;
        page["widgets"].to<JsonArray>().add("gauge");
    }
}

std::string reference(JsonVariantConst value) {
    std::string out;
    serializeJson(value, out);
    return out;
}

class CollectingPrint : public Print {
  public:
    size_t write(uint8_t c) override {
        text.push_back(static_cast<char>(c));
        return 1;
    }
    size_t write(const uint8_t *data, size_t size) override {
        text.append(reinterpret_cast<const char *>(data), size);
        return size;
    }
    std::string text;
};

void assertBufferMatchesArduinoJson(JsonVariantConst value, size_t capacity) {
    std::vector<char> expected(capacity + kGuardBytes, kGuard);
    std::vector<char> actual(capacity + kGuardBytes, kGuard);
    const size_t expectedLength = serializeJson(value, expected.data(), capacity);
    const size_t actualLength = JsonWriter::toBuffer(value, actual.data(), capacity);
    TEST_ASSERT_EQUAL_UINT32(expectedLength, actualLength);
    TEST_ASSERT_EQUAL_MEMORY(expected.data(), actual.data(), expected.size());
}

} // namespace

void setUp() {}

void tearDown() {}

void test_measureMatchesArduinoJson() {
    JsonDocument doc;
    fillSample(doc);
    TEST_ASSERT_EQUAL_UINT32(measureJson(doc), JsonWriter::measure(doc));
    TEST_ASSERT_EQUAL_UINT32(reference(doc).size(), JsonWriter::measure(doc));
}

void test_printReceivesTheSameBytes() {
    JsonDocument doc;
    fillSample(doc);
    CollectingPrint out;
    const size_t written = JsonWriter::toPrint(doc, out);
    TEST_ASSERT_EQUAL_UINT32(out.text.size(), written);
    TEST_ASSERT_EQUAL_STRING(reference(doc).c_str(), out.text.c_str());
}

void test_bufferMatchesArduinoJsonAtEveryCapacity() {
    JsonDocument doc;
    fillSample(doc);
    const size_t full = reference(doc).size();
    for (size_t capacity = 0; capacity <= full + 2; ++capacity) {
        assertBufferMatchesArduinoJson(doc, capacity);
    }
}

void test_scalarsAndEmptyContainersMatchArduinoJson() {
    JsonDocument doc;
    const char *sources[] = {"0", "-1.25", "\"\"", "[]", "{}", "null", "[1,[2,[3]]]"};
    for (const char *source : sources) {
        TEST_ASSERT_FALSE(deserializeJson(doc, source));
        TEST_ASSERT_EQUAL_UINT32(measureJson(doc), JsonWriter::measure(doc));
        assertBufferMatchesArduinoJson(doc, strlen(source) + 1);
        assertBufferMatchesArduinoJson(doc, strlen(source));
    }
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_measureMatchesArduinoJson);
    RUN_TEST(test_printReceivesTheSameBytes);
    RUN_TEST(test_bufferMatchesArduinoJsonAtEveryCapacity);
    RUN_TEST(test_scalarsAndEmptyContainersMatchArduinoJson);
    return UNITY_END();
}
