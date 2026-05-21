// test_main.cpp — Unity tests for UsbEnvelope::findPayloadSlice (#912).
//
// Locks the contract of the allocation-free JSON envelope locator used by
// the PUT_CONFIG path. The function is hot (every config push runs through
// it) and historically tricky:
//   - #576: must work on inputs too large for an in-place JsonDocument.
//   - #884: must not be fooled by an embedded NUL in the wire data
//           (strstr() would short-circuit at the first NUL).
//
// Tests exercise the three modes flagged in the issue: embedded NUL,
// missing key, malformed JSON — plus a few boundary cases (nested objects,
// braces inside string values, whitespace tolerance).

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

} // namespace

void setUp() {}
void tearDown() {}

// Happy path: a well-formed envelope with a flat payload object yields the
// exact byte range from `{` through the matching `}`.
void test_findPayloadSlice_happyPath() {
    const char *line = "{\"cmd\":2,\"payload\":{\"a\":1,\"b\":\"x\"}}";
    size_t outLen = 0;
    const char *slice = UsbEnvelope::findPayloadSlice(line, strlen(line), &outLen);

    TEST_ASSERT_NOT_NULL(slice);
    TEST_ASSERT_TRUE(slicesEqual(slice, outLen, "{\"a\":1,\"b\":\"x\"}"));
}

// Nested objects: depth tracking must descend and return only when the
// outermost `}` closes — not on the first inner `}`.
void test_findPayloadSlice_nestedObject() {
    const char *line = "{\"cmd\":2,\"payload\":{\"outer\":{\"inner\":42}}}";
    size_t outLen = 0;
    const char *slice = UsbEnvelope::findPayloadSlice(line, strlen(line), &outLen);

    TEST_ASSERT_NOT_NULL(slice);
    TEST_ASSERT_TRUE(slicesEqual(slice, outLen, "{\"outer\":{\"inner\":42}}"));
}

// Brace characters inside JSON string values must not be counted by the
// depth tracker. Catches a regression where the string-state machine
// regresses (e.g. forgets to set inString).
void test_findPayloadSlice_bracesInsideStringIgnored() {
    const char *line = "{\"cmd\":2,\"payload\":{\"name\":\"a}b{c\",\"n\":1}}";
    size_t outLen = 0;
    const char *slice = UsbEnvelope::findPayloadSlice(line, strlen(line), &outLen);

    TEST_ASSERT_NOT_NULL(slice);
    TEST_ASSERT_TRUE(slicesEqual(slice, outLen, "{\"name\":\"a}b{c\",\"n\":1}"));
}

// Escaped quote inside a string keeps inString true — the escape skip must
// step past the next byte verbatim. A regression here exits the string mode
// too early and the next `}` closes the envelope prematurely.
void test_findPayloadSlice_escapedQuoteInsideString() {
    const char *line = "{\"cmd\":2,\"payload\":{\"x\":\"a\\\"}\",\"n\":1}}";
    size_t outLen = 0;
    const char *slice = UsbEnvelope::findPayloadSlice(line, strlen(line), &outLen);

    TEST_ASSERT_NOT_NULL(slice);
    TEST_ASSERT_TRUE(slicesEqual(slice, outLen, "{\"x\":\"a\\\"}\",\"n\":1}"));
}

// Whitespace between the key, the colon, and the value must be tolerated —
// the envelope can be reformatted by a host that pretty-prints.
void test_findPayloadSlice_whitespaceTolerance() {
    const char *line = "{\"cmd\":2, \"payload\"  :  \t {\"a\":1}}";
    size_t outLen = 0;
    const char *slice = UsbEnvelope::findPayloadSlice(line, strlen(line), &outLen);

    TEST_ASSERT_NOT_NULL(slice);
    TEST_ASSERT_TRUE(slicesEqual(slice, outLen, "{\"a\":1}"));
}

// Critical: embedded NUL in the wire stream must NOT short-circuit the
// needle search. The locator gets the explicit length and walks the entire
// buffer with memcmp instead of strstr. Issue #884.
void test_findPayloadSlice_embeddedNul_doesNotShortCircuit() {
    // Build a 64-byte buffer: a NUL early on, then the valid envelope past it.
    // strlen() would return 5 here — we pass the real length so the locator
    // sees the rest of the bytes.
    char line[64];
    memset(line, 0, sizeof(line));
    memcpy(line, "abc", 3);                        // pre-NUL noise
    line[3] = '\0';                                // embedded NUL inside the line
    const char tail[] = "{\"payload\":{\"a\":1}}"; // the real envelope
    memcpy(line + 4, tail, sizeof(tail) - 1);
    const size_t lineLen = 4 + (sizeof(tail) - 1);

    size_t outLen = 0;
    const char *slice = UsbEnvelope::findPayloadSlice(line, lineLen, &outLen);

    TEST_ASSERT_NOT_NULL(slice);
    TEST_ASSERT_TRUE(slicesEqual(slice, outLen, "{\"a\":1}"));
}

// Missing key — returns nullptr and leaves *outLen == 0.
void test_findPayloadSlice_missingKey_returnsNull() {
    const char *line = "{\"cmd\":2,\"other\":{\"a\":1}}";
    size_t outLen = 42; // sentinel — must be overwritten to 0
    const char *slice = UsbEnvelope::findPayloadSlice(line, strlen(line), &outLen);

    TEST_ASSERT_NULL(slice);
    TEST_ASSERT_EQUAL_size_t(0, outLen);
}

// Malformed: key present but value is not an object — the locator demands
// the value start with `{`. A string / array / number value yields nullptr.
void test_findPayloadSlice_valueNotObject_returnsNull() {
    const char *line = "{\"cmd\":2,\"payload\":\"not_an_object\"}";
    size_t outLen = 0;
    const char *slice = UsbEnvelope::findPayloadSlice(line, strlen(line), &outLen);

    TEST_ASSERT_NULL(slice);
    TEST_ASSERT_EQUAL_size_t(0, outLen);
}

// Malformed: missing colon after `"payload"`.
void test_findPayloadSlice_missingColon_returnsNull() {
    const char *line = "{\"cmd\":2,\"payload\" {\"a\":1}}";
    size_t outLen = 0;
    const char *slice = UsbEnvelope::findPayloadSlice(line, strlen(line), &outLen);

    TEST_ASSERT_NULL(slice);
    TEST_ASSERT_EQUAL_size_t(0, outLen);
}

// Malformed: payload object never closes (depth stays positive at end).
void test_findPayloadSlice_unterminatedObject_returnsNull() {
    const char *line = "{\"cmd\":2,\"payload\":{\"a\":1";
    size_t outLen = 0;
    const char *slice = UsbEnvelope::findPayloadSlice(line, strlen(line), &outLen);

    TEST_ASSERT_NULL(slice);
    TEST_ASSERT_EQUAL_size_t(0, outLen);
}

// Defensive: nullptr inputs must not crash. ASan in the native env catches a
// regression that dereferences either argument before the null check.
void test_findPayloadSlice_nullInputs_returnNull() {
    size_t outLen = 7;
    TEST_ASSERT_NULL(UsbEnvelope::findPayloadSlice(nullptr, 10, &outLen));

    const char *line = "{\"payload\":{}}";
    TEST_ASSERT_NULL(UsbEnvelope::findPayloadSlice(line, strlen(line), nullptr));
}

// Empty payload object: `{}` is a degenerate but well-formed value.
void test_findPayloadSlice_emptyPayloadObject() {
    const char *line = "{\"cmd\":2,\"payload\":{}}";
    size_t outLen = 0;
    const char *slice = UsbEnvelope::findPayloadSlice(line, strlen(line), &outLen);

    TEST_ASSERT_NOT_NULL(slice);
    TEST_ASSERT_TRUE(slicesEqual(slice, outLen, "{}"));
}

// findNeedle sanity: zero-length needle is an error condition (returns null),
// not a "matches everywhere" sentinel. Documents the contract so a refactor
// that swaps in strstr doesn't drift.
void test_findNeedle_zeroLengthNeedle_returnsNull() {
    const char *hay = "abcdef";
    TEST_ASSERT_NULL(UsbEnvelope::findNeedle(hay, strlen(hay), "needle", 0));
}

// findNeedle: haystack shorter than needle — no match possible.
void test_findNeedle_haystackShorter_returnsNull() {
    TEST_ASSERT_NULL(UsbEnvelope::findNeedle("ab", 2, "abc", 3));
}

int main(int /*argc*/, char ** /*argv*/) {
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
