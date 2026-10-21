#define APP_WORKSHOP_ENABLED 1

#include "hal/usb/usb_reply_route.cpp"

#include "hal/usb/usb_dispatch_get_config.cpp"

#include <Arduino.h>
#include <string>
#include <unity.h>

namespace {

constexpr const char *kDashboard = "{\n  \"version\": \"1.0.0\",\n  \"pages\": []\n}\n";
constexpr const char *kExpectedReply =
    "{\"status\":\"ok\",\"config\":{   \"version\": \"1.0.0\",   \"pages\": [] } }";
constexpr const char *kExpectedTaggedReply =
    "{\"status\":\"ok\",\"config\":{   \"version\": \"1.0.0\",   \"pages\": [] } ,\"seq\":7}";
constexpr UsbSeqEcho::Seq kSeven = {true, 7};
constexpr const char *kNotFound = "{\"status\":\"error\",\"message\":\"config_not_found\"}";

std::string s_socket;
int s_socketLines = 0;

void captureSocket(const char *line, size_t len) {
    s_socket.assign(line, len);
    ++s_socketLines;
}

void stageDashboard() {
    StorageDriver::fakeWrite(kConfigPathDashboard, kDashboard, strlen(kDashboard));
}

char s_getConfig[] = "{\"cmd\":1}";

}

void *Mem::allocPreferSpiram(size_t size) {
    return malloc(size);
}

void UsbComm::sendLine(const char *line) {
    UsbCommInternal::routeLine(line, strlen(line));
}

void UsbComm::sendError(const char *code) {
    char line[96];
    snprintf(line, sizeof(line), "{\"status\":\"error\",\"message\":\"%s\"}", code);
    sendLine(line);
}

void UsbCommInternal::handleCommand(char *) {
    handleGetConfig();
}

void setUp() {
    StorageDriver::fakeReset();
    canshift_test::serialCaptureReset();
    s_socket.clear();
    s_socketLines = 0;
    UsbComm::setSocketSink(captureSocket);
}

void tearDown() {
    UsbComm::setSocketSink(nullptr);
}

void test_socketGetConfigReturnsTheConfigOnTheSocket() {
    stageDashboard();
    UsbComm::dispatchFromSocket(s_getConfig);
    TEST_ASSERT_EQUAL_INT(1, s_socketLines);
    TEST_ASSERT_EQUAL_STRING(kExpectedReply, s_socket.c_str());
    TEST_ASSERT_EQUAL_UINT32(0, canshift_test::serialCaptureLen());
}

void test_serialGetConfigStillStreamsToTheUart() {
    stageDashboard();
    UsbCommInternal::dispatchFromSerial(s_getConfig);
    TEST_ASSERT_EQUAL_INT(0, s_socketLines);
    const std::string expected = std::string(kExpectedReply) + "\n";
    TEST_ASSERT_EQUAL_STRING(expected.c_str(), canshift_test::serialCaptureBuffer());
}

void test_socketGetConfigWithoutAFileAnswersOnTheSocket() {
    UsbComm::dispatchFromSocket(s_getConfig);
    TEST_ASSERT_EQUAL_INT(1, s_socketLines);
    TEST_ASSERT_EQUAL_STRING(kNotFound, s_socket.c_str());
}

void test_socketGetConfigEchoesTheSeq() {
    stageDashboard();
    const UsbSeqEcho::Scope seq(kSeven);
    UsbComm::dispatchFromSocket(s_getConfig);
    TEST_ASSERT_EQUAL_STRING(kExpectedTaggedReply, s_socket.c_str());
}

void test_serialGetConfigEchoesTheSeq() {
    stageDashboard();
    const UsbSeqEcho::Scope seq(kSeven);
    UsbCommInternal::dispatchFromSerial(s_getConfig);
    const std::string expected = std::string(kExpectedTaggedReply) + "\n";
    TEST_ASSERT_EQUAL_STRING(expected.c_str(), canshift_test::serialCaptureBuffer());
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_socketGetConfigReturnsTheConfigOnTheSocket);
    RUN_TEST(test_serialGetConfigStillStreamsToTheUart);
    RUN_TEST(test_socketGetConfigWithoutAFileAnswersOnTheSocket);
    RUN_TEST(test_socketGetConfigEchoesTheSeq);
    RUN_TEST(test_serialGetConfigEchoesTheSeq);
    return UNITY_END();
}
