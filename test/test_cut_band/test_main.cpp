#include "alert_engine_rs.h"
#include "runtime/signal_store.h"
#include "ui/cut_sources.h"

#include <Arduino.h>
#include <string.h>
#include <unity.h>

namespace {

constexpr uint8_t kBoostCut = 0;
constexpr uint8_t kFuelCut = 1;
constexpr uint8_t kIgnitionRetard = 3;
constexpr uint8_t kLimpMode = 8;

constexpr const char *kExpectedNames[kAlertCutKindCount] = {
    "BOOST CUT",    "FUEL CUT",      "IGNITION CUT",     "IGNITION RETARD", "REV LIMIT",
    "TRACTION CUT", "PIT LIMIT CUT", "OVERHEAT PROTECT", "LIMP MODE"};

constexpr SignalId kExpectedFlags[kAlertCutKindCount] = {
    SignalIds::kFlagBoostCut,       SignalIds::kFlagFuelCut,         SignalIds::kFlagIgnitionCut,
    SignalIds::kFlagIgnitionRetard, SignalIds::kFlagRevLimit,        SignalIds::kFlagTractionCut,
    SignalIds::kFlagPitLimitCut,    SignalIds::kFlagOverheatProtect, SignalIds::kFlagLimpMode};

CutBandStateRs makeState() {
    CutBandStateRs state;
    cut_band_reset_rs(&state);
    return state;
}

uint8_t rowsAt(const CutBandStateRs &state, uint32_t nowMs, CutRowRs *out) {
    return cut_band_rows_rs(&state, nowMs, out);
}

uint16_t flagsNow() {
    SignalStore::SignalValue snap[kSignalStoreMaxSignals];
    SignalStore::snapshotAll(snap);
    return CutSources::activeFlags(snap);
}

void detailNow(uint8_t kind, char *out, size_t len) {
    SignalStore::SignalValue snap[kSignalStoreMaxSignals];
    SignalStore::snapshotAll(snap);
    CutSources::composeDetail(kind, snap, out, len);
}

uint16_t bit(uint8_t kind) {
    return static_cast<uint16_t>(1u << kind);
}

}

void setUp() {
    mockSetMillis(0);
    SignalStore::init();
}

void tearDown() {}

void test_kind_names_come_from_the_shared_table() {
    for (uint8_t kind = 0; kind < kAlertCutKindCount; ++kind) {
        TEST_ASSERT_EQUAL_STRING(kExpectedNames[kind], cut_kind_name_rs(kind));
        TEST_ASSERT_EQUAL_UINT8(kExpectedFlags[kind], CutSources::kSources[kind].flag);
    }
}

void test_unknown_kind_renders_no_name() {
    TEST_ASSERT_EQUAL_STRING("", cut_kind_name_rs(kAlertCutKindCount));
    TEST_ASSERT_EQUAL_UINT8(SignalIds::kSignalCount,
                            CutSources::offendingSignal(kAlertCutKindCount));
    TEST_ASSERT_EQUAL_UINT16(0, CutSources::activeFlags(nullptr));
}

void test_flag_bitmask_tracks_the_signal_store() {
    TEST_ASSERT_EQUAL_UINT16(0, flagsNow());
    SignalStore::update(SignalIds::kFlagFuelCut, 1.0f);
    TEST_ASSERT_EQUAL_UINT16(bit(kFuelCut), flagsNow());
    SignalStore::update(SignalIds::kFlagFuelCut, 0.0f);
    TEST_ASSERT_EQUAL_UINT16(0, flagsNow());
}

void test_band_appears_on_the_frame_the_flag_arrives() {
    CutBandStateRs state = makeState();
    CutRowRs rows[kAlertCutRowCapacity];

    cut_band_step_rs(&state, 0, 1000);
    TEST_ASSERT_EQUAL_UINT8(0, rowsAt(state, 1000, rows));

    cut_band_step_rs(&state, bit(kBoostCut), 1000);
    TEST_ASSERT_EQUAL_UINT8(1, rowsAt(state, 1000, rows));
    TEST_ASSERT_EQUAL_UINT8(kBoostCut, rows[0].kind);
    TEST_ASSERT_EQUAL_UINT32(0, rows[0].elapsed_ms);
}

void test_a_sixty_millisecond_cut_holds_the_minimum() {
    CutBandStateRs state = makeState();
    CutRowRs rows[kAlertCutRowCapacity];

    cut_band_step_rs(&state, bit(kBoostCut), 0);
    cut_band_step_rs(&state, 0, 60);
    TEST_ASSERT_EQUAL_UINT8(1, rowsAt(state, 60, rows));

    cut_band_step_rs(&state, 0, kAlertCutMinVisibleMs - 1);
    TEST_ASSERT_EQUAL_UINT8(1, rowsAt(state, kAlertCutMinVisibleMs - 1, rows));
    TEST_ASSERT_EQUAL_UINT32(60, rows[0].elapsed_ms);

    cut_band_step_rs(&state, 0, kAlertCutMinVisibleMs);
    TEST_ASSERT_EQUAL_UINT8(0, rowsAt(state, kAlertCutMinVisibleMs, rows));
}

void test_stacks_three_most_severe_first() {
    CutBandStateRs state = makeState();
    CutRowRs rows[kAlertCutRowCapacity];

    const uint16_t flags = static_cast<uint16_t>(bit(kBoostCut) | bit(kIgnitionRetard) |
                                                 bit(kFuelCut) | bit(kLimpMode));
    cut_band_step_rs(&state, flags, 0);
    TEST_ASSERT_EQUAL_UINT8(kAlertCutRowCapacity, rowsAt(state, 0, rows));
    TEST_ASSERT_EQUAL_UINT8(kFuelCut, rows[0].kind);
    TEST_ASSERT_EQUAL_UINT8(kAlertSeverityFailure, rows[0].severity);
    TEST_ASSERT_EQUAL_UINT8(kLimpMode, rows[1].kind);
    TEST_ASSERT_EQUAL_UINT8(kBoostCut, rows[2].kind);
    TEST_ASSERT_EQUAL_UINT8(kAlertSeverityWarning, rows[2].severity);
}

void test_readout_style_is_per_kind() {
    CutBandStateRs state = makeState();
    CutRowRs rows[kAlertCutRowCapacity];

    cut_band_step_rs(&state, static_cast<uint16_t>(bit(kLimpMode) | bit(kIgnitionRetard)), 0);
    TEST_ASSERT_EQUAL_UINT8(2, rowsAt(state, 0, rows));
    TEST_ASSERT_EQUAL_UINT8(kAlertCutReadoutLatched, rows[0].readout);
    TEST_ASSERT_EQUAL_UINT8(kAlertCutReadoutHolding, rows[1].readout);
}

void test_detail_reads_the_measured_values() {
    char detail[64];
    SignalStore::update(SignalIds::kBoostBar, 1.94f);
    SignalStore::update(SignalIds::kBoostTargetBar, 1.80f);
    detailNow(kBoostCut, detail, sizeof(detail));
    TEST_ASSERT_EQUAL_STRING("OVERBOOST 1.94 \xc2\xb7 LIMIT 1.80", detail);

    SignalStore::update(SignalIds::kOilPressBar, 1.1f);
    detailNow(kFuelCut, detail, sizeof(detail));
    TEST_ASSERT_EQUAL_STRING("OIL PRESS 1.1 \xc2\xb7 ENGINE PROTECT", detail);
}

void test_detail_skips_a_clause_with_no_data() {
    char detail[64];
    SignalStore::update(SignalIds::kKnockCount, 4.0f);
    detailNow(kIgnitionRetard, detail, sizeof(detail));
    TEST_ASSERT_EQUAL_STRING("4 EVENTS", detail);

    SignalStore::update(SignalIds::kKnockCyl, 3.0f);
    detailNow(kIgnitionRetard, detail, sizeof(detail));
    TEST_ASSERT_EQUAL_STRING("KNOCK CYL 3 \xc2\xb7 4 EVENTS", detail);
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_kind_names_come_from_the_shared_table);
    RUN_TEST(test_unknown_kind_renders_no_name);
    RUN_TEST(test_flag_bitmask_tracks_the_signal_store);
    RUN_TEST(test_band_appears_on_the_frame_the_flag_arrives);
    RUN_TEST(test_a_sixty_millisecond_cut_holds_the_minimum);
    RUN_TEST(test_stacks_three_most_severe_first);
    RUN_TEST(test_readout_style_is_per_kind);
    RUN_TEST(test_detail_reads_the_measured_values);
    RUN_TEST(test_detail_skips_a_clause_with_no_data);
    return UNITY_END();
}
