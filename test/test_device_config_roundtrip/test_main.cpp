#include "hal/usb/usb_config_sync.cpp"

#include "config/config_loader.h"

#include <string>
#include <unity.h>

namespace {

constexpr const char *kPushedDevice =
    R"({"cmd":4,"device_config":{"can_speed_kbps":250,"twai_tx_pin":5,"twai_rx_pin":4}})";
constexpr const char *kReadBack =
    R"({"status":"ok","device_config":{"can_speed_kbps":250,"twai_tx_pin":5,"twai_rx_pin":4}})";
constexpr const char *kLegacyWrappedFile =
    R"({"device_config":{"can_speed_kbps":250,"twai_tx_pin":5,"twai_rx_pin":4}})";
constexpr const char *kPushedInputs =
    R"({"cmd":7,"input_bindings":[{"id":"btn_a","pin":13,"action":"next_page"}]})";
constexpr const char *kInputsReadBack =
    R"({"status":"ok","input_bindings":[{"id":"btn_a","pin":13,"action":"next_page"}]})";
constexpr const char *kOkRebooting = "ok_rebooting";

std::string s_lastLine;

void put(const char *json, void (*handler)(const JsonObjectConst &)) {
    JsonDocument doc;
    TEST_ASSERT_TRUE(deserializeJson(doc, json) == DeserializationError::Ok);
    handler(doc.as<JsonObjectConst>());
}

void getDevice() {
    UsbCommInternal::sendTypedConfigGet(kConfigPathDevice, TypedConfigBody::kDeviceConfigKey,
                                        TypedConfigBody::device);
}

void assertDeviceLoaded() {
    TEST_ASSERT_TRUE(ConfigLoader::loadAll().deviceOk);
    const CfgDeviceConfig &device = ConfigLoader::getDeviceConfig();
    TEST_ASSERT_EQUAL_INT(250, device.canSpeedKbps);
    TEST_ASSERT_EQUAL_INT(5, device.twaiTxPin);
    TEST_ASSERT_EQUAL_INT(4, device.twaiRxPin);
}

} // namespace

void *Mem::allocPreferSpiram(size_t size) {
    return malloc(size);
}

void UsbComm::sendLine(const char *line) {
    s_lastLine = line;
}

void UsbComm::sendError(const char *code) {
    s_lastLine = code;
}

void UsbComm::sendOkRebooting() {
    s_lastLine = kOkRebooting;
}

void UsbComm::flushAndRestart() {}

void setUp() {
    StorageDriver::fakeReset();
    s_lastLine.clear();
}

void tearDown() {}

void test_pushedDeviceConfigIsReadByTheLoaderAfterReboot() {
    put(kPushedDevice, UsbCommInternal::handlePutDeviceConfig);
    TEST_ASSERT_EQUAL_STRING(kOkRebooting, s_lastLine.c_str());
    assertDeviceLoaded();
}

void test_pushedDeviceConfigReadsBackOnceWrapped() {
    put(kPushedDevice, UsbCommInternal::handlePutDeviceConfig);
    getDevice();
    TEST_ASSERT_EQUAL_STRING(kReadBack, s_lastLine.c_str());
}

void test_fileWrittenByTheOldFirmwareStillLoadsAndReadsBack() {
    StorageDriver::fakeWrite(kConfigPathDevice, kLegacyWrappedFile, strlen(kLegacyWrappedFile));
    assertDeviceLoaded();
    getDevice();
    TEST_ASSERT_EQUAL_STRING(kReadBack, s_lastLine.c_str());
}

void test_inputBindingsStillRoundTrip() {
    put(kPushedInputs, UsbCommInternal::handlePutInputBindings);
    UsbCommInternal::sendTypedConfigGet(kConfigPathInputs, TypedConfigBody::kInputBindingsKey,
                                        TypedConfigBody::inputBindings);
    TEST_ASSERT_EQUAL_STRING(kInputsReadBack, s_lastLine.c_str());
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_pushedDeviceConfigIsReadByTheLoaderAfterReboot);
    RUN_TEST(test_pushedDeviceConfigReadsBackOnceWrapped);
    RUN_TEST(test_fileWrittenByTheOldFirmwareStillLoadsAndReadsBack);
    RUN_TEST(test_inputBindingsStillRoundTrip);
    return UNITY_END();
}
