
#include "can/signal_map.h"

#include <string.h>
#include <unity.h>

void setUp() {}
void tearDown() {}

void test_known_engine_signals() {
    TEST_ASSERT_EQUAL_UINT8(SignalIds::kRpm, signalIdFromName("rpm"));
    TEST_ASSERT_EQUAL_UINT8(SignalIds::kThrottlePos, signalIdFromName("throttle_pos"));
    TEST_ASSERT_EQUAL_UINT8(SignalIds::kMapKpa, signalIdFromName("map_kpa"));
    TEST_ASSERT_EQUAL_UINT8(SignalIds::kBoostBar, signalIdFromName("boost_bar"));
    TEST_ASSERT_EQUAL_UINT8(SignalIds::kIatC, signalIdFromName("iat_c"));
}

void test_known_temp_pressure_fuel_signals() {
    TEST_ASSERT_EQUAL_UINT8(SignalIds::kCoolantTempC, signalIdFromName("coolant_temp_c"));
    TEST_ASSERT_EQUAL_UINT8(SignalIds::kOilTempC, signalIdFromName("oil_temp_c"));
    TEST_ASSERT_EQUAL_UINT8(SignalIds::kOilPressBar, signalIdFromName("oil_press_bar"));
    TEST_ASSERT_EQUAL_UINT8(SignalIds::kFuelPressBar, signalIdFromName("fuel_press_bar"));
    TEST_ASSERT_EQUAL_UINT8(SignalIds::kLambda1, signalIdFromName("lambda_1"));
    TEST_ASSERT_EQUAL_UINT8(SignalIds::kAfr1, signalIdFromName("afr_1"));
}

void test_dash_page_signals() {
    TEST_ASSERT_EQUAL_UINT8(SignalIds::kFuelLevelPct, signalIdFromName("fuel_level_pct"));
    TEST_ASSERT_EQUAL_UINT8(SignalIds::kEgtC, signalIdFromName("egt_c"));
    TEST_ASSERT_EQUAL_UINT8(SignalIds::kGearboxTempC, signalIdFromName("gearbox_temp_c"));
    TEST_ASSERT_EQUAL_UINT8(SignalIds::kDiffTempC, signalIdFromName("diff_temp_c"));
    TEST_ASSERT_EQUAL_UINT8(SignalIds::kKnockCount, signalIdFromName("knock_count"));
    TEST_ASSERT_EQUAL_UINT8(SignalIds::kClutchState, signalIdFromName("clutch_state"));
    TEST_ASSERT_EQUAL_UINT8(SignalIds::kOdoKm, signalIdFromName("odo_km"));
    TEST_ASSERT_EQUAL_UINT8(SignalIds::kTripKm, signalIdFromName("trip_km"));
}

void test_known_vehicle_electrical_signals() {
    TEST_ASSERT_EQUAL_UINT8(SignalIds::kSpeedKph, signalIdFromName("speed_kph"));
    TEST_ASSERT_EQUAL_UINT8(SignalIds::kGear, signalIdFromName("gear"));
    TEST_ASSERT_EQUAL_UINT8(SignalIds::kBatteryVolts, signalIdFromName("battery_volts"));
}

void test_known_flag_signals() {
    TEST_ASSERT_EQUAL_UINT8(SignalIds::kFlagMil, signalIdFromName("flag_mil"));
    TEST_ASSERT_EQUAL_UINT8(SignalIds::kFlagLaunchCtrl, signalIdFromName("flag_launch_ctrl"));
    TEST_ASSERT_EQUAL_UINT8(SignalIds::kFlagFlatShift, signalIdFromName("flag_flat_shift"));
    TEST_ASSERT_EQUAL_UINT8(SignalIds::kFlagAntiLag, signalIdFromName("flag_anti_lag"));
    TEST_ASSERT_EQUAL_UINT8(SignalIds::kFlagTractionCut, signalIdFromName("flag_traction_cut"));
}

void test_known_cut_flag_signals() {
    TEST_ASSERT_EQUAL_UINT8(SignalIds::kFlagBoostCut, signalIdFromName("flag_boost_cut"));
    TEST_ASSERT_EQUAL_UINT8(SignalIds::kFlagFuelCut, signalIdFromName("flag_fuel_cut"));
    TEST_ASSERT_EQUAL_UINT8(SignalIds::kFlagIgnitionCut, signalIdFromName("flag_ignition_cut"));
    TEST_ASSERT_EQUAL_UINT8(SignalIds::kFlagIgnitionRetard,
                            signalIdFromName("flag_ignition_retard"));
    TEST_ASSERT_EQUAL_UINT8(SignalIds::kFlagRevLimit, signalIdFromName("flag_rev_limit"));
    TEST_ASSERT_EQUAL_UINT8(SignalIds::kFlagPitLimitCut, signalIdFromName("flag_pit_limit_cut"));
    TEST_ASSERT_EQUAL_UINT8(SignalIds::kFlagOverheatProtect,
                            signalIdFromName("flag_overheat_protect"));
    TEST_ASSERT_EQUAL_UINT8(SignalIds::kFlagLimpMode, signalIdFromName("flag_limp_mode"));
    TEST_ASSERT_EQUAL_UINT8(SignalIds::kKnockCyl, signalIdFromName("knock_cyl"));
    TEST_ASSERT_EQUAL_UINT8(SignalIds::kBoostTargetBar, signalIdFromName("boost_target_bar"));
}

void test_known_map_and_lap_signals() {
    TEST_ASSERT_EQUAL_UINT8(SignalIds::kMapNumber, signalIdFromName("map_number"));
    TEST_ASSERT_EQUAL_UINT8(SignalIds::kMapNameIdx, signalIdFromName("map_name_idx"));
    TEST_ASSERT_EQUAL_UINT8(SignalIds::kLapTimerMs, signalIdFromName("lap_timer_ms"));
}

void test_wheel_button_slots() {
    TEST_ASSERT_EQUAL_UINT8(SignalIds::kWheelButton1, signalIdFromName("wheel_button_1"));
    TEST_ASSERT_EQUAL_UINT8(SignalIds::kWheelButton2, signalIdFromName("wheel_button_2"));
    TEST_ASSERT_EQUAL_UINT8(SignalIds::kWheelButton3, signalIdFromName("wheel_button_3"));
    TEST_ASSERT_EQUAL_UINT8(SignalIds::kWheelButton4, signalIdFromName("wheel_button_4"));
}

void test_unknown_returns_sentinel() {
    TEST_ASSERT_EQUAL_UINT8(SignalIds::kSignalCount, signalIdFromName("not_a_signal"));
    TEST_ASSERT_EQUAL_UINT8(SignalIds::kSignalCount, signalIdFromName(""));
    // Partial / suffix variants must not match — `strcmp` is exact.
    TEST_ASSERT_EQUAL_UINT8(SignalIds::kSignalCount, signalIdFromName("rpm_extra"));
    TEST_ASSERT_EQUAL_UINT8(SignalIds::kSignalCount, signalIdFromName("RPM"));
    // Substring of a known name — also must miss.
    TEST_ASSERT_EQUAL_UINT8(SignalIds::kSignalCount, signalIdFromName("rp"));
}

void test_null_returns_sentinel() {
    TEST_ASSERT_EQUAL_UINT8(SignalIds::kSignalCount, signalIdFromName(nullptr));
}

void test_sentinel_value_locked() {
    // The Rust port hard-codes SIGNAL_COUNT = 64 in `rust/signal-map/src/lib.rs`;
    // failing this assert means the C++ and Rust sentinels have drifted.
    TEST_ASSERT_EQUAL_UINT8(64, SignalIds::kSignalCount);
}

int main(int /*argc*/, char ** /*argv*/) {
    UNITY_BEGIN();
    RUN_TEST(test_known_engine_signals);
    RUN_TEST(test_known_temp_pressure_fuel_signals);
    RUN_TEST(test_dash_page_signals);
    RUN_TEST(test_known_vehicle_electrical_signals);
    RUN_TEST(test_known_flag_signals);
    RUN_TEST(test_known_cut_flag_signals);
    RUN_TEST(test_known_map_and_lap_signals);
    RUN_TEST(test_wheel_button_slots);
    RUN_TEST(test_unknown_returns_sentinel);
    RUN_TEST(test_null_returns_sentinel);
    RUN_TEST(test_sentinel_value_locked);
    return UNITY_END();
}
