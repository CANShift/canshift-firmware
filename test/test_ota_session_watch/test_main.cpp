#include "runtime/ota_session_watch.h"

#include <unity.h>

namespace {

using OtaSessionWatch::Session;
using OtaSessionWatch::Verdict;

constexpr uint32_t kStartMs = 1000;

Session receivingOver(bool fromSocket) {
    return Session{true, fromSocket, kStartMs};
}

uint8_t verdictOf(const Session &session, bool socketClosed, uint32_t nowMs) {
    return static_cast<uint8_t>(OtaSessionWatch::judge(session, socketClosed, nowMs));
}

void test_aFlowingSessionIsKept() {
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Verdict::Keep),
                            verdictOf(receivingOver(false), false, kStartMs + 500));
}

void test_aSilentSessionTimesOut() {
    const Session session = receivingOver(false);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Verdict::Keep),
                            verdictOf(session, false, kStartMs + OtaSessionWatch::kIdleTimeoutMs));
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(Verdict::IdleTimeout),
        verdictOf(session, false, kStartMs + OtaSessionWatch::kIdleTimeoutMs + 1));
}

void test_theTimeoutSurvivesAMillisWrap() {
    const Session session{true, false, UINT32_MAX - 100};
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Verdict::Keep), verdictOf(session, false, 200));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Verdict::IdleTimeout),
                            verdictOf(session, false, OtaSessionWatch::kIdleTimeoutMs));
}

void test_closingTheSocketEndsASocketSession() {
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Verdict::LinkClosed),
                            verdictOf(receivingOver(true), true, kStartMs + 10));
}

void test_closingTheSocketLeavesAUsbSessionAlone() {
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Verdict::Keep),
                            verdictOf(receivingOver(false), true, kStartMs + 10));
}

void test_noSessionIsNeverAborted() {
    const Session idle{false, true, kStartMs};
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Verdict::Keep),
                            verdictOf(idle, true, kStartMs + 10 * OtaSessionWatch::kIdleTimeoutMs));
}

}

void setUp() {}

void tearDown() {}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_aFlowingSessionIsKept);
    RUN_TEST(test_aSilentSessionTimesOut);
    RUN_TEST(test_theTimeoutSurvivesAMillisWrap);
    RUN_TEST(test_closingTheSocketEndsASocketSession);
    RUN_TEST(test_closingTheSocketLeavesAUsbSessionAlone);
    RUN_TEST(test_noSessionIsNeverAborted);
    return UNITY_END();
}
