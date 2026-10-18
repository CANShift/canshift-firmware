#include "runtime/signal_store.h"
#include "ui/control_status.h"
#include "ui/control_vocabulary.h"

#include <Arduino.h>
#include <unity.h>

namespace {

using ControlVocabulary::ControlId;
using ControlVocabulary::ControlState;

constexpr float kFlagOn = 1.0f;
constexpr float kFlagOff = 0.0f;
constexpr float kFirstMap = 1.0f;
constexpr float kThirdMap = 3.0f;
constexpr uint8_t kThirdMapLevel = 3;
constexpr ControlStatus::Request kIdleRequest = {false, 0};

void dropFlag(SignalId flag) {
    SignalStore::update(flag, kFlagOn);
    SignalStore::update(flag, kFlagOff);
}

void test_droppedFlagStopsConfirmingOnTheNextFrame() {
    dropFlag(SignalIds::kFlagCruise);
    TEST_ASSERT_FALSE(ControlStatus::isConfirming(SignalIds::kFlagCruise));
}

void test_releasedBrakeClearsTheCruiseGuardOnTheNextFrame() {
    dropFlag(SignalIds::kFlagBrake);
    const ControlVocabulary::Control *cruise = ControlVocabulary::find("CRUISE");
    TEST_ASSERT_NOT_NULL(cruise);
    const ControlState state =
        ControlStatus::evaluate(*cruise, SignalIds::kFlagCruise, kIdleRequest);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ControlState::OFF), static_cast<uint8_t>(state));
    TEST_ASSERT_EQUAL_FLOAT(kFlagOff, ControlStatus::guardReading(ControlId::CRUISE).value);
}

void test_ecuMapLevelJumpsWithoutWalkingIntermediateLevels() {
    SignalStore::update(SignalIds::kMapNumber, kFirstMap);
    SignalStore::update(SignalIds::kMapNumber, kThirdMap);
    uint8_t level = 0;
    TEST_ASSERT_TRUE(ControlStatus::levelFromSignal(ControlId::ECU_MAP, &level));
    TEST_ASSERT_EQUAL_UINT8(kThirdMapLevel, level);
}

}

void setUp() {
    mockSetMillis(0);
    SignalStore::init();
}

void tearDown() {}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_droppedFlagStopsConfirmingOnTheNextFrame);
    RUN_TEST(test_releasedBrakeClearsTheCruiseGuardOnTheNextFrame);
    RUN_TEST(test_ecuMapLevelJumpsWithoutWalkingIntermediateLevels);
    return UNITY_END();
}
