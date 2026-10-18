#include "hal/usb/usb_status_line.h"

#include <ArduinoJson.h>
#include <string.h>
#include <unity.h>

namespace {

constexpr size_t kLineCap = 160;
constexpr unsigned kProtocol = 3;

}

void setUp() {}
void tearDown() {}

void test_plainFieldsKeepTheWireOrder() {
    char line[kLineCap];
    const UsbStatusLine::Fields fields = {"0.20.1", kProtocol, true, "waveshare_s3_28"};
    TEST_ASSERT_GREATER_THAN(0, UsbStatusLine::format(fields, line, sizeof(line)));
    TEST_ASSERT_EQUAL_STRING(
        "{\"status\":\"ok\",\"version\":\"0.20.1\",\"protocol\":3,\"is_day\":1,"
        "\"board_id\":\"waveshare_s3_28\"}",
        line);
}

void test_boardIdWithQuotesAndBackslashStaysOneString() {
    char line[kLineCap];
    const char *hostile = "x\",\"is_day\":0,\"y\":\"\\";
    const UsbStatusLine::Fields fields = {"0.20.1", kProtocol, true, hostile};
    TEST_ASSERT_GREATER_THAN(0, UsbStatusLine::format(fields, line, sizeof(line)));

    JsonDocument doc;
    TEST_ASSERT_TRUE(deserializeJson(doc, line) == DeserializationError::Ok);
    TEST_ASSERT_EQUAL_STRING(hostile, doc["board_id"].as<const char *>());
    TEST_ASSERT_EQUAL_INT(1, doc["is_day"].as<int>());
}

void test_lineThatDoesNotFitIsRefused() {
    char line[16];
    const UsbStatusLine::Fields fields = {"0.20.1", kProtocol, false, "waveshare_s3_28"};
    TEST_ASSERT_EQUAL_UINT32(0, UsbStatusLine::format(fields, line, sizeof(line)));
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_plainFieldsKeepTheWireOrder);
    RUN_TEST(test_boardIdWithQuotesAndBackslashStaysOneString);
    RUN_TEST(test_lineThatDoesNotFitIsRefused);
    return UNITY_END();
}
