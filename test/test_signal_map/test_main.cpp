
#include "can/signal_map.h"

#include <string.h>
#include <unity.h>

void setUp() {}
void tearDown() {}

void test_known_engine_signals() {
    TEST_ASSERT_EQUAL_UINT8(SignalIds::RPM, signalIdFromName("rpm"));
    TEST_ASSERT_EQUAL_UINT8(SignalIds::THROTTLE_POS, signalIdFromName("throttle_pos"));
    TEST_ASSERT_EQUAL_UINT8(SignalIds::MAP_KPA, signalIdFromName("map_kpa"));
    TEST_ASSERT_EQUAL_UINT8(SignalIds::BOOST_BAR, signalIdFromName("boost_bar"));
    TEST_ASSERT_EQUAL_UINT8(SignalIds::IAT_C, signalIdFromName("iat_c"));
}

void test_known_temp_pressure_fuel_signals() {
    TEST_ASSERT_EQUAL_UINT8(SignalIds::COOLANT_TEMP_C, signalIdFromName("coolant_temp_c"));
    TEST_ASSERT_EQUAL_UINT8(SignalIds::OIL_TEMP_C, signalIdFromName("oil_temp_c"));
    TEST_ASSERT_EQUAL_UINT8(SignalIds::OIL_PRESS_BAR, signalIdFromName("oil_press_bar"));
    TEST_ASSERT_EQUAL_UINT8(SignalIds::FUEL_PRESS_BAR, signalIdFromName("fuel_press_bar"));
    TEST_ASSERT_EQUAL_UINT8(SignalIds::LAMBDA_1, signalIdFromName("lambda_1"));
    TEST_ASSERT_EQUAL_UINT8(SignalIds::AFR_1, signalIdFromName("afr_1"));
}

void test_known_vehicle_electrical_signals() {
    TEST_ASSERT_EQUAL_UINT8(SignalIds::SPEED_KPH, signalIdFromName("speed_kph"));
    TEST_ASSERT_EQUAL_UINT8(SignalIds::GEAR, signalIdFromName("gear"));
    TEST_ASSERT_EQUAL_UINT8(SignalIds::BATTERY_VOLTS, signalIdFromName("battery_volts"));
}

void test_known_flag_signals() {
    TEST_ASSERT_EQUAL_UINT8(SignalIds::FLAG_MIL, signalIdFromName("flag_mil"));
    TEST_ASSERT_EQUAL_UINT8(SignalIds::FLAG_LAUNCH_CTRL, signalIdFromName("flag_launch_ctrl"));
    TEST_ASSERT_EQUAL_UINT8(SignalIds::FLAG_FLAT_SHIFT, signalIdFromName("flag_flat_shift"));
    TEST_ASSERT_EQUAL_UINT8(SignalIds::FLAG_ANTI_LAG, signalIdFromName("flag_anti_lag"));
    TEST_ASSERT_EQUAL_UINT8(SignalIds::FLAG_TRACTION_CUT, signalIdFromName("flag_traction_cut"));
}

void test_known_map_and_lap_signals() {
    TEST_ASSERT_EQUAL_UINT8(SignalIds::MAP_NUMBER, signalIdFromName("map_number"));
    TEST_ASSERT_EQUAL_UINT8(SignalIds::MAP_NAME_IDX, signalIdFromName("map_name_idx"));
    TEST_ASSERT_EQUAL_UINT8(SignalIds::LAP_TIMER_MS, signalIdFromName("lap_timer_ms"));
}

void test_unknown_returns_sentinel() {
    TEST_ASSERT_EQUAL_UINT8(SignalIds::SIGNAL_COUNT, signalIdFromName("not_a_signal"));
    TEST_ASSERT_EQUAL_UINT8(SignalIds::SIGNAL_COUNT, signalIdFromName(""));
    // Partial / suffix variants must not match — `strcmp` is exact.
    TEST_ASSERT_EQUAL_UINT8(SignalIds::SIGNAL_COUNT, signalIdFromName("rpm_extra"));
    TEST_ASSERT_EQUAL_UINT8(SignalIds::SIGNAL_COUNT, signalIdFromName("RPM"));
    // Substring of a known name — also must miss.
    TEST_ASSERT_EQUAL_UINT8(SignalIds::SIGNAL_COUNT, signalIdFromName("rp"));
}

void test_null_returns_sentinel() {
    TEST_ASSERT_EQUAL_UINT8(SignalIds::SIGNAL_COUNT, signalIdFromName(nullptr));
}

void test_sentinel_value_locked() {
    // The Rust port hard-codes SIGNAL_COUNT = 64 in `rust/signal-map/src/lib.rs`;
    // failing this assert means the C++ and Rust sentinels have drifted.
    TEST_ASSERT_EQUAL_UINT8(64, SignalIds::SIGNAL_COUNT);
}

int main(int /*argc*/, char ** /*argv*/) {
    UNITY_BEGIN();
    RUN_TEST(test_known_engine_signals);
    RUN_TEST(test_known_temp_pressure_fuel_signals);
    RUN_TEST(test_known_vehicle_electrical_signals);
    RUN_TEST(test_known_flag_signals);
    RUN_TEST(test_known_map_and_lap_signals);
    RUN_TEST(test_unknown_returns_sentinel);
    RUN_TEST(test_null_returns_sentinel);
    RUN_TEST(test_sentinel_value_locked);
    return UNITY_END();
}
