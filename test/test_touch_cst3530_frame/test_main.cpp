#include "touch_cst3530_frame.h"

#include <string.h>
#include <unity.h>

using namespace canshift::touch::cst3530;

namespace {

struct PointBytes {
    uint8_t xLow;
    uint8_t yLow;
    uint8_t pressure;
    uint8_t high;
    uint8_t eventAndId;
};

void seal(uint8_t *frame, uint8_t keys, uint8_t touched) {
    frame[kStatusByte] = kFrameReady;
    frame[kCountByte] = static_cast<uint8_t>((keys << 4) | touched);
    uint16_t sum = kChecksumSeed;
    const size_t payload = static_cast<size_t>(keys + touched) * kPointStride;
    for (size_t i = 0; i < payload; ++i) {
        sum = static_cast<uint16_t>(sum + frame[kFirstPointByte + i]);
    }
    frame[0] = static_cast<uint8_t>(sum & 0xFF);
    frame[1] = static_cast<uint8_t>(sum >> 8);
}

void writePoint(uint8_t *frame, uint8_t slot, const PointBytes &p) {
    uint8_t *base = frame + kFirstPointByte + static_cast<size_t>(slot) * kPointStride;
    base[0] = p.xLow;
    base[1] = p.yLow;
    base[2] = p.pressure;
    base[3] = p.high;
    base[4] = p.eventAndId;
}

PointBytes pointAt(uint16_t x, uint16_t y, uint8_t pressure, uint8_t id) {
    return PointBytes{static_cast<uint8_t>(x & 0xFF), static_cast<uint8_t>(y & 0xFF), pressure,
                      static_cast<uint8_t>(((y >> 4) & 0xF0) | ((x >> 8) & 0x0F)),
                      static_cast<uint8_t>(0x10 | id)};
}

} // namespace

void setUp() {}
void tearDown() {}

void test_singlePoint_decodesCoordinates() {
    uint8_t frame[kFrameBytes] = {};
    writePoint(frame, 0, pointAt(300, 285, 42, 3));
    seal(frame, 0, 1);

    Sample samples[kMaxPoints] = {};
    TEST_ASSERT_EQUAL_UINT8(1, decodeFrame(frame, samples, kMaxPoints));
    TEST_ASSERT_EQUAL_UINT16(300, samples[0].x);
    TEST_ASSERT_EQUAL_UINT16(285, samples[0].y);
    TEST_ASSERT_EQUAL_UINT8(42, samples[0].pressure);
    TEST_ASSERT_EQUAL_UINT8(3, samples[0].id);
}

void test_twoPoints_decodeInOrder() {
    uint8_t frame[kFrameBytes] = {};
    writePoint(frame, 0, pointAt(10, 20, 5, 0));
    writePoint(frame, 1, pointAt(200, 310, 7, 1));
    seal(frame, 0, 2);

    Sample samples[kMaxPoints] = {};
    TEST_ASSERT_EQUAL_UINT8(2, decodeFrame(frame, samples, kMaxPoints));
    TEST_ASSERT_EQUAL_UINT16(10, samples[0].x);
    TEST_ASSERT_EQUAL_UINT16(200, samples[1].x);
    TEST_ASSERT_EQUAL_UINT16(310, samples[1].y);
    TEST_ASSERT_EQUAL_UINT8(1, samples[1].id);
}

void test_keySlotsShiftThePointBase() {
    uint8_t frame[kFrameBytes] = {};
    writePoint(frame, 0, pointAt(999, 999, 99, 9));
    writePoint(frame, 1, pointAt(120, 160, 11, 2));
    seal(frame, 1, 1);

    Sample samples[kMaxPoints] = {};
    TEST_ASSERT_EQUAL_UINT8(1, decodeFrame(frame, samples, kMaxPoints));
    TEST_ASSERT_EQUAL_UINT16(120, samples[0].x);
    TEST_ASSERT_EQUAL_UINT16(160, samples[0].y);
}

void test_zeroEvent_pointIsDropped() {
    uint8_t frame[kFrameBytes] = {};
    PointBytes released = pointAt(50, 60, 1, 0);
    released.eventAndId = 0x00;
    writePoint(frame, 0, released);
    seal(frame, 0, 1);

    Sample samples[kMaxPoints] = {};
    TEST_ASSERT_EQUAL_UINT8(0, decodeFrame(frame, samples, kMaxPoints));
}

void test_statusByteNotReady_rejected() {
    uint8_t frame[kFrameBytes] = {};
    writePoint(frame, 0, pointAt(10, 20, 5, 0));
    seal(frame, 0, 1);
    frame[kStatusByte] = 0x00;

    Sample samples[kMaxPoints] = {};
    TEST_ASSERT_EQUAL_UINT8(0, decodeFrame(frame, samples, kMaxPoints));
}

void test_brokenChecksum_rejected() {
    uint8_t frame[kFrameBytes] = {};
    writePoint(frame, 0, pointAt(10, 20, 5, 0));
    seal(frame, 0, 1);
    frame[kFirstPointByte] = static_cast<uint8_t>(frame[kFirstPointByte] + 1);

    Sample samples[kMaxPoints] = {};
    TEST_ASSERT_EQUAL_UINT8(0, decodeFrame(frame, samples, kMaxPoints));
}

void test_countOutOfRange_rejected() {
    uint8_t frame[kFrameBytes] = {};
    writePoint(frame, 0, pointAt(10, 20, 5, 0));
    seal(frame, 0, 1);

    Sample samples[kMaxPoints] = {};
    frame[kCountByte] = 0x00;
    TEST_ASSERT_EQUAL_UINT8(0, decodeFrame(frame, samples, kMaxPoints));
    frame[kCountByte] = 0x06;
    TEST_ASSERT_EQUAL_UINT8(0, decodeFrame(frame, samples, kMaxPoints));
}

void test_capacityClampsReportedPoints() {
    uint8_t frame[kFrameBytes] = {};
    writePoint(frame, 0, pointAt(10, 20, 5, 0));
    writePoint(frame, 1, pointAt(30, 40, 6, 1));
    seal(frame, 0, 2);

    Sample samples[kMaxPoints] = {};
    TEST_ASSERT_EQUAL_UINT8(1, decodeFrame(frame, samples, 1));
    TEST_ASSERT_EQUAL_UINT16(10, samples[0].x);
}

void test_fiveFingers_allDecode() {
    uint8_t frame[kFrameBytes] = {};
    for (uint8_t slot = 0; slot < kMaxPoints; ++slot) {
        writePoint(frame, slot, pointAt(static_cast<uint16_t>(10 * (slot + 1)), 20, 5, slot));
    }
    seal(frame, 0, kMaxPoints);

    Sample samples[kMaxPoints] = {};
    TEST_ASSERT_EQUAL_UINT8(kMaxPoints, decodeFrame(frame, samples, kMaxPoints));
    TEST_ASSERT_EQUAL_UINT16(50, samples[4].x);
}

void test_payloadBeyondTheFrame_rejected() {
    uint8_t frame[kFrameBytes] = {};
    writePoint(frame, 0, pointAt(10, 20, 5, 0));
    seal(frame, 0, 1);
    frame[kCountByte] = static_cast<uint8_t>((2 << 4) | kMaxPoints);

    Sample samples[kMaxPoints] = {};
    TEST_ASSERT_EQUAL_UINT8(0, decodeFrame(frame, samples, kMaxPoints));
}

void test_nullArgumentsRejected() {
    uint8_t frame[kFrameBytes] = {};
    writePoint(frame, 0, pointAt(10, 20, 5, 0));
    seal(frame, 0, 1);

    Sample samples[kMaxPoints] = {};
    TEST_ASSERT_EQUAL_UINT8(0, decodeFrame(nullptr, samples, kMaxPoints));
    TEST_ASSERT_EQUAL_UINT8(0, decodeFrame(frame, nullptr, kMaxPoints));
    TEST_ASSERT_EQUAL_UINT8(0, decodeFrame(frame, samples, 0));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_singlePoint_decodesCoordinates);
    RUN_TEST(test_twoPoints_decodeInOrder);
    RUN_TEST(test_keySlotsShiftThePointBase);
    RUN_TEST(test_zeroEvent_pointIsDropped);
    RUN_TEST(test_statusByteNotReady_rejected);
    RUN_TEST(test_brokenChecksum_rejected);
    RUN_TEST(test_countOutOfRange_rejected);
    RUN_TEST(test_capacityClampsReportedPoints);
    RUN_TEST(test_fiveFingers_allDecode);
    RUN_TEST(test_payloadBeyondTheFrame_rejected);
    RUN_TEST(test_nullArgumentsRejected);
    return UNITY_END();
}
