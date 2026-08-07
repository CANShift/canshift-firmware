#include "hal/ble/telemetry_frame.h"

#include <cmath>
#include <stddef.h>
#include <stdint.h>
#include <unity.h>

void setUp() {}
void tearDown() {}

namespace {

void clearFields(TelemetryFrame::Field *fields, size_t count) {
    for (size_t i = 0; i < count; i++) {
        fields[i].present = false;
        fields[i].value = 0.0f;
    }
}

} // namespace

void test_encode_emptyFrame_headerOnly() {
    TelemetryFrame::Field fields[14];
    clearFields(fields, 14);
    uint8_t buf[TelemetryFrame::MAX_FRAME_BYTES] = {0};
    const size_t len = TelemetryFrame::encode(fields, 14, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_size_t(3, len);
    const uint8_t expected[] = {0x01, 0x00, 0x00};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, buf, 3);
}

void test_encode_canonicalVector_matchesCore() {
    // Shared byte vector pinned in canshift-core telemetry-frame.test.ts:
    // { r: 3500 (i0), tps: 45.5 (i1), g: -1 (i12) }
    TelemetryFrame::Field fields[14];
    clearFields(fields, 14);
    fields[0].present = true;
    fields[0].value = 3500.0f;
    fields[1].present = true;
    fields[1].value = 45.5f;
    fields[12].present = true;
    fields[12].value = -1.0f;

    uint8_t buf[TelemetryFrame::MAX_FRAME_BYTES] = {0};
    const size_t len = TelemetryFrame::encode(fields, 14, buf, sizeof(buf));

    const uint8_t expected[] = {0x01, 0x03, 0x10, 0xe0, 0x67, 0x35, 0x00, 0xbc,
                                0xb1, 0x00, 0x00, 0x18, 0xfc, 0xff, 0xff};
    TEST_ASSERT_EQUAL_size_t(sizeof(expected), len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, buf, sizeof(expected));
}

void test_encode_skipsNonFinite() {
    TelemetryFrame::Field fields[14];
    clearFields(fields, 14);
    fields[0].present = true;
    fields[0].value = std::nanf("");
    fields[1].present = true;
    fields[1].value = 40.0f;

    uint8_t buf[TelemetryFrame::MAX_FRAME_BYTES] = {0};
    const size_t len = TelemetryFrame::encode(fields, 14, buf, sizeof(buf));

    const uint8_t expected[] = {0x01, 0x02, 0x00, 0x40, 0x9c, 0x00, 0x00};
    TEST_ASSERT_EQUAL_size_t(sizeof(expected), len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, buf, sizeof(expected));
}

void test_encode_rejectsTooSmallBuffer() {
    TelemetryFrame::Field fields[14];
    clearFields(fields, 14);
    fields[0].present = true;
    fields[0].value = 100.0f;

    uint8_t buf[4] = {0};
    TEST_ASSERT_EQUAL_size_t(0, TelemetryFrame::encode(fields, 14, buf, sizeof(buf)));
}

void test_encode_rejectsNullAndOverCount() {
    TelemetryFrame::Field fields[14];
    clearFields(fields, 14);
    uint8_t buf[TelemetryFrame::MAX_FRAME_BYTES] = {0};
    TEST_ASSERT_EQUAL_size_t(0, TelemetryFrame::encode(nullptr, 14, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_size_t(0, TelemetryFrame::encode(fields, 14, nullptr, sizeof(buf)));
    TEST_ASSERT_EQUAL_size_t(0, TelemetryFrame::encode(fields, 15, buf, sizeof(buf)));
}

void test_dedup_sendsFirstFrame() {
    TelemetryFrame::Deduper dedup = {};
    const uint8_t frame[] = {0x01, 0x01, 0x00, 0x10, 0x27, 0x00, 0x00};
    TEST_ASSERT_TRUE(TelemetryFrame::shouldSend(dedup, frame, sizeof(frame)));
}

void test_dedup_skipsUnchangedWithinKeepalive() {
    TelemetryFrame::Deduper dedup = {};
    const uint8_t frame[] = {0x01, 0x01, 0x00, 0x10, 0x27, 0x00, 0x00};
    TEST_ASSERT_TRUE(TelemetryFrame::shouldSend(dedup, frame, sizeof(frame)));
    for (uint16_t i = 1; i < TelemetryFrame::KEEPALIVE_TICKS; i++)
        TEST_ASSERT_FALSE(TelemetryFrame::shouldSend(dedup, frame, sizeof(frame)));
}

void test_dedup_resendsOnKeepaliveTick() {
    TelemetryFrame::Deduper dedup = {};
    const uint8_t frame[] = {0x01, 0x01, 0x00, 0x10, 0x27, 0x00, 0x00};
    TEST_ASSERT_TRUE(TelemetryFrame::shouldSend(dedup, frame, sizeof(frame)));
    for (uint16_t i = 1; i < TelemetryFrame::KEEPALIVE_TICKS; i++)
        TelemetryFrame::shouldSend(dedup, frame, sizeof(frame));
    TEST_ASSERT_TRUE(TelemetryFrame::shouldSend(dedup, frame, sizeof(frame)));
}

void test_dedup_sendsWhenPayloadChanges() {
    TelemetryFrame::Deduper dedup = {};
    const uint8_t a[] = {0x01, 0x01, 0x00, 0x10, 0x27, 0x00, 0x00};
    const uint8_t b[] = {0x01, 0x01, 0x00, 0x11, 0x27, 0x00, 0x00};
    TEST_ASSERT_TRUE(TelemetryFrame::shouldSend(dedup, a, sizeof(a)));
    TEST_ASSERT_TRUE(TelemetryFrame::shouldSend(dedup, b, sizeof(b)));
}

void test_dedup_sendsWhenLengthChanges() {
    TelemetryFrame::Deduper dedup = {};
    const uint8_t a[] = {0x01, 0x01, 0x00, 0x10, 0x27, 0x00, 0x00};
    const uint8_t b[] = {0x01, 0x00, 0x00};
    TEST_ASSERT_TRUE(TelemetryFrame::shouldSend(dedup, a, sizeof(a)));
    TEST_ASSERT_TRUE(TelemetryFrame::shouldSend(dedup, b, sizeof(b)));
}

void test_dedup_resetForcesResend() {
    TelemetryFrame::Deduper dedup = {};
    const uint8_t frame[] = {0x01, 0x01, 0x00, 0x10, 0x27, 0x00, 0x00};
    TEST_ASSERT_TRUE(TelemetryFrame::shouldSend(dedup, frame, sizeof(frame)));
    TEST_ASSERT_FALSE(TelemetryFrame::shouldSend(dedup, frame, sizeof(frame)));
    TelemetryFrame::resetDeduper(dedup);
    TEST_ASSERT_TRUE(TelemetryFrame::shouldSend(dedup, frame, sizeof(frame)));
}

void test_dedup_rejectsInvalidFrame() {
    TelemetryFrame::Deduper dedup = {};
    const uint8_t frame[] = {0x01, 0x00, 0x00};
    TEST_ASSERT_FALSE(TelemetryFrame::shouldSend(dedup, nullptr, sizeof(frame)));
    TEST_ASSERT_FALSE(TelemetryFrame::shouldSend(dedup, frame, 0));
    TEST_ASSERT_FALSE(
        TelemetryFrame::shouldSend(dedup, frame, TelemetryFrame::MAX_FRAME_BYTES + 1));
}

int main(int /*argc*/, char ** /*argv*/) {
    UNITY_BEGIN();
    RUN_TEST(test_encode_emptyFrame_headerOnly);
    RUN_TEST(test_encode_canonicalVector_matchesCore);
    RUN_TEST(test_encode_skipsNonFinite);
    RUN_TEST(test_encode_rejectsTooSmallBuffer);
    RUN_TEST(test_encode_rejectsNullAndOverCount);
    RUN_TEST(test_dedup_sendsFirstFrame);
    RUN_TEST(test_dedup_skipsUnchangedWithinKeepalive);
    RUN_TEST(test_dedup_resendsOnKeepaliveTick);
    RUN_TEST(test_dedup_sendsWhenPayloadChanges);
    RUN_TEST(test_dedup_sendsWhenLengthChanges);
    RUN_TEST(test_dedup_resetForcesResend);
    RUN_TEST(test_dedup_rejectsInvalidFrame);
    return UNITY_END();
}
