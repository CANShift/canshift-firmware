
#include "hal/usb/usb_envelope.h"

#include <string.h>
#include <unity.h>

namespace {

bool slicesEqual(const char *got, size_t gotLen, const char *expected) {
    const size_t expectedLen = strlen(expected);
    if (gotLen != expectedLen)
        return false;
    return memcmp(got, expected, gotLen) == 0;
}

}

void setUp() {}
void tearDown() {}

void test_findPayloadSlice_happyPath() {
    const char *line = "{\"cmd\":2,\"payload\":{\"a\":1,\"b\":\"x\"}}";
    size_t outLen = 0;
    const char *slice = UsbEnvelope::findPayloadSlice(line, strlen(line), &outLen);

    TEST_ASSERT_NOT_NULL(slice);
    TEST_ASSERT_TRUE(slicesEqual(slice, outLen, "{\"a\":1,\"b\":\"x\"}"));
}

void test_findPayloadSlice_nestedObject() {
    const char *line = "{\"cmd\":2,\"payload\":{\"outer\":{\"inner\":42}}}";
    size_t outLen = 0;
    const char *slice = UsbEnvelope::findPayloadSlice(line, strlen(line), &outLen);

    TEST_ASSERT_NOT_NULL(slice);
    TEST_ASSERT_TRUE(slicesEqual(slice, outLen, "{\"outer\":{\"inner\":42}}"));
}

void test_findPayloadSlice_bracesInsideStringIgnored() {
    const char *line = "{\"cmd\":2,\"payload\":{\"name\":\"a}b{c\",\"n\":1}}";
    size_t outLen = 0;
    const char *slice = UsbEnvelope::findPayloadSlice(line, strlen(line), &outLen);

    TEST_ASSERT_NOT_NULL(slice);
    TEST_ASSERT_TRUE(slicesEqual(slice, outLen, "{\"name\":\"a}b{c\",\"n\":1}"));
}

void test_findPayloadSlice_escapedQuoteInsideString() {
    const char *line = "{\"cmd\":2,\"payload\":{\"x\":\"a\\\"}\",\"n\":1}}";
    size_t outLen = 0;
    const char *slice = UsbEnvelope::findPayloadSlice(line, strlen(line), &outLen);

    TEST_ASSERT_NOT_NULL(slice);
    TEST_ASSERT_TRUE(slicesEqual(slice, outLen, "{\"x\":\"a\\\"}\",\"n\":1}"));
}

void test_findPayloadSlice_whitespaceTolerance() {
    const char *line = "{\"cmd\":2, \"payload\"  :  \t {\"a\":1}}";
    size_t outLen = 0;
    const char *slice = UsbEnvelope::findPayloadSlice(line, strlen(line), &outLen);

    TEST_ASSERT_NOT_NULL(slice);
    TEST_ASSERT_TRUE(slicesEqual(slice, outLen, "{\"a\":1}"));
}

void test_findPayloadSlice_embeddedNul_doesNotShortCircuit() {
    char line[64];
    memset(line, 0, sizeof(line));
    memcpy(line, "abc", 3);
    line[3] = '\0';
    const char tail[] = "{\"payload\":{\"a\":1}}";
    memcpy(line + 4, tail, sizeof(tail) - 1);
    const size_t lineLen = 4 + (sizeof(tail) - 1);

    size_t outLen = 0;
    const char *slice = UsbEnvelope::findPayloadSlice(line, lineLen, &outLen);

    TEST_ASSERT_NOT_NULL(slice);
    TEST_ASSERT_TRUE(slicesEqual(slice, outLen, "{\"a\":1}"));
}

void test_findPayloadSlice_missingKey_returnsNull() {
    const char *line = "{\"cmd\":2,\"other\":{\"a\":1}}";
    size_t outLen = 42;
    const char *slice = UsbEnvelope::findPayloadSlice(line, strlen(line), &outLen);

    TEST_ASSERT_NULL(slice);
    TEST_ASSERT_EQUAL_size_t(0, outLen);
}

void test_findPayloadSlice_valueNotObject_returnsNull() {
    const char *line = "{\"cmd\":2,\"payload\":\"not_an_object\"}";
    size_t outLen = 0;
    const char *slice = UsbEnvelope::findPayloadSlice(line, strlen(line), &outLen);

    TEST_ASSERT_NULL(slice);
    TEST_ASSERT_EQUAL_size_t(0, outLen);
}

void test_findPayloadSlice_missingColon_returnsNull() {
    const char *line = "{\"cmd\":2,\"payload\" {\"a\":1}}";
    size_t outLen = 0;
    const char *slice = UsbEnvelope::findPayloadSlice(line, strlen(line), &outLen);

    TEST_ASSERT_NULL(slice);
    TEST_ASSERT_EQUAL_size_t(0, outLen);
}

void test_findPayloadSlice_unterminatedObject_returnsNull() {
    const char *line = "{\"cmd\":2,\"payload\":{\"a\":1";
    size_t outLen = 0;
    const char *slice = UsbEnvelope::findPayloadSlice(line, strlen(line), &outLen);

    TEST_ASSERT_NULL(slice);
    TEST_ASSERT_EQUAL_size_t(0, outLen);
}

void test_findPayloadSlice_nullInputs_returnNull() {
    size_t outLen = 7;
    TEST_ASSERT_NULL(UsbEnvelope::findPayloadSlice(nullptr, 10, &outLen));

    const char *line = "{\"payload\":{}}";
    TEST_ASSERT_NULL(UsbEnvelope::findPayloadSlice(line, strlen(line), nullptr));
}

void test_findPayloadSlice_emptyPayloadObject() {
    const char *line = "{\"cmd\":2,\"payload\":{}}";
    size_t outLen = 0;
    const char *slice = UsbEnvelope::findPayloadSlice(line, strlen(line), &outLen);

    TEST_ASSERT_NOT_NULL(slice);
    TEST_ASSERT_TRUE(slicesEqual(slice, outLen, "{}"));
}

void test_findNeedle_zeroLengthNeedle_returnsNull() {
    const char *hay = "abcdef";
    TEST_ASSERT_NULL(UsbEnvelope::findNeedle(hay, strlen(hay), "needle", 0));
}

void test_findNeedle_haystackShorter_returnsNull() {
    TEST_ASSERT_NULL(UsbEnvelope::findNeedle("ab", 2, "abc", 3));
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_findPayloadSlice_happyPath);
    RUN_TEST(test_findPayloadSlice_nestedObject);
    RUN_TEST(test_findPayloadSlice_bracesInsideStringIgnored);
    RUN_TEST(test_findPayloadSlice_escapedQuoteInsideString);
    RUN_TEST(test_findPayloadSlice_whitespaceTolerance);
    RUN_TEST(test_findPayloadSlice_embeddedNul_doesNotShortCircuit);
    RUN_TEST(test_findPayloadSlice_missingKey_returnsNull);
    RUN_TEST(test_findPayloadSlice_valueNotObject_returnsNull);
    RUN_TEST(test_findPayloadSlice_missingColon_returnsNull);
    RUN_TEST(test_findPayloadSlice_unterminatedObject_returnsNull);
    RUN_TEST(test_findPayloadSlice_nullInputs_returnNull);
    RUN_TEST(test_findPayloadSlice_emptyPayloadObject);
    RUN_TEST(test_findNeedle_zeroLengthNeedle_returnsNull);
    RUN_TEST(test_findNeedle_haystackShorter_returnsNull);
    return UNITY_END();
}
