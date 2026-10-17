#include "hal/usb/usb_seq_echo.h"

#include <string.h>
#include <unity.h>

namespace {

constexpr size_t kCap = UsbSeqEcho::kMaxTaggedLine;
constexpr const char *kOk = "{\"status\":\"ok\"}";

size_t tagOk(UsbSeqEcho::Seq seq, char *out) {
    return UsbSeqEcho::tag(kOk, strlen(kOk), seq, out, kCap);
}

} // namespace

void setUp() {}
void tearDown() {}

void test_parse_readsTheSeqRightAfterTheCmd() {
    const UsbSeqEcho::Seq seq = UsbSeqEcho::parseAfterCmd(",\"seq\":42}");
    TEST_ASSERT_TRUE(seq.present);
    TEST_ASSERT_EQUAL_UINT32(42, seq.value);
}

void test_parse_withoutSeqAfterTheCmd_isAbsent() {
    TEST_ASSERT_FALSE(UsbSeqEcho::parseAfterCmd("}").present);
    TEST_ASSERT_FALSE(UsbSeqEcho::parseAfterCmd(",\"payload\":{},\"seq\":9}").present);
    TEST_ASSERT_FALSE(UsbSeqEcho::parseAfterCmd(nullptr).present);
}

void test_parse_rejectsANonNumberOrAnOverflow() {
    TEST_ASSERT_FALSE(UsbSeqEcho::parseAfterCmd(",\"seq\":\"7\"}").present);
    TEST_ASSERT_FALSE(UsbSeqEcho::parseAfterCmd(",\"seq\":-1}").present);
    TEST_ASSERT_FALSE(UsbSeqEcho::parseAfterCmd(",\"seq\":4294967296}").present);
}

void test_tag_appendsTheSeqToAStatusLine() {
    char out[kCap];
    TEST_ASSERT_GREATER_THAN(0, tagOk({true, 7}, out));
    TEST_ASSERT_EQUAL_STRING("{\"status\":\"ok\",\"seq\":7}", out);
}

void test_tag_leavesTheLineAloneWithoutSeq() {
    char out[kCap];
    TEST_ASSERT_EQUAL_size_t(0, tagOk({false, 0}, out));
}

void test_tag_leavesNonStatusLinesAlone() {
    char out[kCap];
    const char *frame = "{\"type\":\"tele\",\"r\":3000}";
    TEST_ASSERT_EQUAL_size_t(0, UsbSeqEcho::tag(frame, strlen(frame), {true, 7}, out, kCap));
}

void test_tag_leavesALineThatWouldNotFitAlone() {
    char out[16];
    TEST_ASSERT_EQUAL_size_t(0, UsbSeqEcho::tag(kOk, strlen(kOk), {true, 7}, out, sizeof(out)));
}

int main(int /*argc*/, char ** /*argv*/) {
    UNITY_BEGIN();
    RUN_TEST(test_parse_readsTheSeqRightAfterTheCmd);
    RUN_TEST(test_parse_withoutSeqAfterTheCmd_isAbsent);
    RUN_TEST(test_parse_rejectsANonNumberOrAnOverflow);
    RUN_TEST(test_tag_appendsTheSeqToAStatusLine);
    RUN_TEST(test_tag_leavesTheLineAloneWithoutSeq);
    RUN_TEST(test_tag_leavesNonStatusLinesAlone);
    RUN_TEST(test_tag_leavesALineThatWouldNotFitAlone);
    return UNITY_END();
}
