#include "board_config.h"
#include "config/config_loader.h"
#include "config/config_loader_internal.h"
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

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_keeps_valid_binding_and_drops_invalid_ones);
    RUN_TEST(test_missing_file_loads_no_bindings);
    return UNITY_END();
}
