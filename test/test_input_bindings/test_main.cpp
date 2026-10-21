#include "board_config.h"
#include "config/config_loader.h"
#include "config/config_loader_internal.h"
#include "diag/error_store.h"
#include "hal/storage/storage_driver.h"

#include <string.h>
#include <unity.h>

namespace {

constexpr char kInputBindingsJson[] =
    "{\"input_bindings\":["
    "{\"id\":\"ok\",\"pin\":4,\"debounce_ms\":900,\"signal\":\"wheel_button_1\","
    "\"action\":{\"type\":\"navigate\",\"pageId\":\"p1\"}},"
    "{\"id\":\"far\",\"pin\":50,\"action\":{\"type\":\"navigate\",\"pageId\":\"p1\"}},"
    "{\"id\":\"bare\",\"pin\":5},"
    "{\"id\":\"odd\",\"pin\":6,\"action\":{\"type\":\"teleport\"}}"
    "]}";

void stageInputBindings(const char *json) {
    StorageDriver::fakeReset();
    StorageDriver::fakeWrite(kConfigPathInputs, json, strlen(json));
}

const CfgDeviceConfig &loadDevice(const char *json) {
    StorageDriver::fakeReset();
    ErrorStore::clear();
    StorageDriver::fakeWrite(kConfigPathDevice, json, strlen(json));
    TEST_ASSERT_TRUE(ConfigLoaderInternal::loadDevice());
    return ConfigLoader::getDeviceConfig();
}

bool errorStoreHolds(const char *code) {
    FwError rows[8];
    uint8_t count = 0;
    ErrorStore::getAll(rows, &count, 8);
    for (uint8_t i = 0; i < count; ++i) {
        if (strcmp(rows[i].code, code) == 0)
            return true;
    }
    return false;
}

}

void setUp() {}
void tearDown() {}

void test_keeps_valid_binding_and_drops_invalid_ones() {
    stageInputBindings(kInputBindingsJson);

    TEST_ASSERT_TRUE(ConfigLoaderInternal::loadInputBindings());

    const CfgInputBindings &inputs = ConfigLoader::getInputBindings();
    TEST_ASSERT_EQUAL_UINT8(1, inputs.count);
    TEST_ASSERT_EQUAL_STRING("ok", inputs.bindings[0].id);
    TEST_ASSERT_EQUAL_INT8(4, inputs.bindings[0].pin);
    TEST_ASSERT_EQUAL_UINT16(500, inputs.bindings[0].debounceMs);
    TEST_ASSERT_EQUAL_STRING("wheel_button_1", inputs.bindings[0].signal);
    TEST_ASSERT_EQUAL_STRING("p1", inputs.bindings[0].action.pageId);
}

void test_missing_file_loads_no_bindings() {
    StorageDriver::fakeReset();

    TEST_ASSERT_FALSE(ConfigLoaderInternal::loadInputBindings());
    TEST_ASSERT_EQUAL_UINT8(0, ConfigLoader::getInputBindings().count);
}

void test_device_keeps_free_twai_pins() {
    const CfgDeviceConfig &dev = loadDevice(R"({"twai_tx_pin":5,"twai_rx_pin":4})");
    TEST_ASSERT_EQUAL_INT8(5, dev.twaiTxPin);
    TEST_ASSERT_EQUAL_INT8(4, dev.twaiRxPin);
    TEST_ASSERT_FALSE(errorStoreHolds("PIN_REJECT"));
}

void test_device_rejects_flash_and_display_pins() {
    const CfgDeviceConfig &dev = loadDevice(R"({"twai_tx_pin":6,"twai_rx_pin":13})");
    TEST_ASSERT_EQUAL_INT8(-1, dev.twaiTxPin);
    TEST_ASSERT_EQUAL_INT8(-1, dev.twaiRxPin);
    TEST_ASSERT_TRUE(errorStoreHolds("PIN_REJECT"));
}

void test_device_input_only_pin_is_rx_only() {
    const CfgDeviceConfig &dev = loadDevice(R"({"twai_tx_pin":34,"twai_rx_pin":34})");
    TEST_ASSERT_EQUAL_INT8(-1, dev.twaiTxPin);
    TEST_ASSERT_EQUAL_INT8(34, dev.twaiRxPin);
}

void test_binding_on_board_twai_or_flash_pin_is_dropped() {
    stageInputBindings(
        "{\"input_bindings\":["
        "{\"id\":\"can\",\"pin\":22,\"action\":{\"type\":\"navigate\",\"pageId\":\"p\"}},"
        "{\"id\":\"flash\",\"pin\":7,\"action\":{\"type\":\"navigate\",\"pageId\":\"p\"}},"
        "{\"id\":\"in\",\"pin\":39,\"action\":{\"type\":\"navigate\",\"pageId\":\"p\"}}"
        "]}");
    TEST_ASSERT_TRUE(ConfigLoaderInternal::loadInputBindings());
    const CfgInputBindings &inputs = ConfigLoader::getInputBindings();
    TEST_ASSERT_EQUAL_UINT8(1, inputs.count);
    TEST_ASSERT_EQUAL_STRING("in", inputs.bindings[0].id);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_keeps_valid_binding_and_drops_invalid_ones);
    RUN_TEST(test_missing_file_loads_no_bindings);
    RUN_TEST(test_device_keeps_free_twai_pins);
    RUN_TEST(test_device_rejects_flash_and_display_pins);
    RUN_TEST(test_device_input_only_pin_is_rx_only);
    RUN_TEST(test_binding_on_board_twai_or_flash_pin_is_dropped);
    return UNITY_END();
}
