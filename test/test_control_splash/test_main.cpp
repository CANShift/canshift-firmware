#include "can/signal_map.h"
#include "control_state_rs.h"
#include "runtime/signal_store.h"
#include "ui/control_splash_content.h"
#include "ui/control_vocabulary.h"

#include <Arduino.h>
#include <string.h>
#include <unity.h>

namespace {

using ControlSplashContent::Content;
using ControlSplashCopy::Bottom;
using ControlSplashCopy::Sub;
using ControlSplashCopy::Tone;
using ControlVocabulary::ControlState;

Content composeFor(const char *kicker, ControlState state, uint8_t level) {
    const ControlVocabulary::Control *control = ControlVocabulary::find(kicker);
    TEST_ASSERT_NOT_NULL(control);
    Content content;
    ControlSplashContent::compose(*control, state, level, content);
    return content;
}

void feedAntiLag(float egtC) {
    SignalStore::update(SignalIds::EGT_C, egtC);
}

} // namespace

void setUp() {
    mockSetMillis(0);
    SignalStore::init();
}

void tearDown() {}

void test_s01_antiLagEngaged() {
    feedAntiLag(912.0f);
    const Content content = composeFor("ANTI-LAG", ControlState::ACTIVE, 0);
    TEST_ASSERT_EQUAL(Tone::ENGAGE, content.tone);
    TEST_ASSERT_EQUAL_STRING("ANTI-LAG", content.name);
    TEST_ASSERT_EQUAL_STRING("ON", content.hero);
    TEST_ASSERT_EQUAL(Sub::NONE, content.sub);
    TEST_ASSERT_EQUAL(Bottom::CELLS, content.bottom);
    TEST_ASSERT_EQUAL_UINT8(2, content.cellCount);
    TEST_ASSERT_EQUAL_STRING("EGT", content.cells[0].kicker);
    TEST_ASSERT_EQUAL_STRING("912", content.cells[0].value);
    TEST_ASSERT_EQUAL_STRING(" °C", content.cells[0].unit);
    TEST_ASSERT_EQUAL_STRING("CUT LIMIT", content.cells[1].kicker);
    TEST_ASSERT_EQUAL_STRING("950", content.cells[1].value);
}

void test_s02_antiLagOff() {
    feedAntiLag(906.0f);
    const Content content = composeFor("ANTI-LAG", ControlState::OFF, 0);
    TEST_ASSERT_EQUAL(Tone::DISENGAGE, content.tone);
    TEST_ASSERT_EQUAL_STRING("OFF", content.hero);
    TEST_ASSERT_EQUAL(Sub::NONE, content.sub);
    TEST_ASSERT_EQUAL(Bottom::LINE, content.bottom);
    TEST_ASSERT_EQUAL_STRING("EGT FALLING · 906 °C", content.line);
}

void test_s03_launchArmed() {
    SignalStore::update(SignalIds::RPM, 4200.0f);
    const Content content = composeFor("LAUNCH", ControlState::ARMED, 0);
    TEST_ASSERT_EQUAL(Tone::ENGAGE, content.tone);
    TEST_ASSERT_EQUAL_STRING("LAUNCH CONTROL", content.name);
    TEST_ASSERT_EQUAL_STRING("4200", content.hero);
    TEST_ASSERT_EQUAL_STRING(" rpm", content.heroUnit);
    TEST_ASSERT_EQUAL(Sub::STATE_WORD, content.sub);
    TEST_ASSERT_EQUAL_STRING("ARMED", content.subText);
    TEST_ASSERT_EQUAL_STRING("CLUTCH IN · RELEASE TO GO", content.line);
}

void test_s04_tractionLevelChanged() {
    const Content content = composeFor("TRACTION", ControlState::ARMED, 4);
    TEST_ASSERT_EQUAL(Tone::ENGAGE, content.tone);
    TEST_ASSERT_EQUAL_STRING("TRACTION CONTROL", content.name);
    TEST_ASSERT_EQUAL_STRING("4", content.hero);
    TEST_ASSERT_EQUAL(Sub::LEVEL_NOTE, content.sub);
    TEST_ASSERT_EQUAL_STRING("OF 6 · MORE SLIP", content.subText);
    TEST_ASSERT_EQUAL(Bottom::SEGMENTS, content.bottom);
    TEST_ASSERT_EQUAL_UINT8(4, content.segmentsLit);
}

void test_s05_ecuMapChanged() {
    const Content content = composeFor("ECU MAP", ControlState::OFF, 2);
    TEST_ASSERT_EQUAL(Tone::ENGAGE, content.tone);
    TEST_ASSERT_EQUAL_STRING("ECU MAP", content.name);
    TEST_ASSERT_EQUAL_STRING("2", content.hero);
    TEST_ASSERT_EQUAL(Sub::NONE, content.sub);
    TEST_ASSERT_EQUAL(Bottom::CELLS, content.bottom);
}

void test_s06_refused() {
    SignalStore::update(SignalIds::SPEED_KPH, 24.0f);
    const Content content = composeFor("LAUNCH", ControlState::UNAVAILABLE, 0);
    TEST_ASSERT_EQUAL(Tone::REFUSE, content.tone);
    TEST_ASSERT_EQUAL_STRING("LAUNCH CONTROL", content.name);
    TEST_ASSERT_EQUAL_STRING("LOCKED", content.hero);
    TEST_ASSERT_EQUAL(Sub::REASON, content.sub);
    TEST_ASSERT_EQUAL_STRING("CAR IS MOVING — 24 km/h", content.subText);
    TEST_ASSERT_EQUAL(Bottom::LINE, content.bottom);
    TEST_ASSERT_EQUAL_STRING("STOP TO ARM", content.line);
}

void test_aStepperBackToZeroDisengages() {
    const Content content = composeFor("TRACTION", ControlState::OFF, 0);
    TEST_ASSERT_EQUAL(Tone::DISENGAGE, content.tone);
    TEST_ASSERT_EQUAL_STRING("OFF", content.hero);
    TEST_ASSERT_EQUAL(Sub::NONE, content.sub);
    TEST_ASSERT_EQUAL(Bottom::NONE, content.bottom);
}

void test_aStaleGuardNeverInventsAReading() {
    const Content content = composeFor("ANTI-LAG", ControlState::OFF, 0);
    TEST_ASSERT_EQUAL_STRING("EGT FALLING", content.line);
    const Content refused = composeFor("ANTI-LAG", ControlState::UNAVAILABLE, 0);
    TEST_ASSERT_EQUAL_STRING("EGT IS HIGH", refused.subText);
    TEST_ASSERT_EQUAL_UINT8(0, refused.cellCount);
}

void test_aHeldValueNeverRepeatsItselfUnderTheHero() {
    SignalStore::update(SignalIds::SPEED_KPH, 60.0f);
    const Content content = composeFor("PIT LIMIT", ControlState::ACTIVE, 0);
    TEST_ASSERT_EQUAL_STRING("60", content.hero);
    TEST_ASSERT_EQUAL(Sub::NONE, content.sub);
}

void test_theChangeWindowIs800msAndTheRefusal1200() {
    TEST_ASSERT_EQUAL_UINT32(CONTROL_SPLASH_CHANGE_MS,
                             control_splash_hold_ms_rs(CONTROL_SPLASH_KIND_CHANGE));
    TEST_ASSERT_EQUAL_UINT32(CONTROL_SPLASH_REFUSAL_MS,
                             control_splash_hold_ms_rs(CONTROL_SPLASH_KIND_REFUSAL));

    ControlSplashRs timer = {};
    control_splash_raise_rs(&timer, CONTROL_SPLASH_KIND_CHANGE, 1000);
    TEST_ASSERT_TRUE(control_splash_poll_rs(&timer, 1000 + CONTROL_SPLASH_CHANGE_MS - 1));
    TEST_ASSERT_FALSE(control_splash_poll_rs(&timer, 1000 + CONTROL_SPLASH_CHANGE_MS));

    control_splash_raise_rs(&timer, CONTROL_SPLASH_KIND_REFUSAL, 0);
    TEST_ASSERT_TRUE(control_splash_poll_rs(&timer, CONTROL_SPLASH_REFUSAL_MS - 1));
    TEST_ASSERT_FALSE(control_splash_poll_rs(&timer, CONTROL_SPLASH_REFUSAL_MS));
}

void test_aSecondChangeReplacesTheFirstInsteadOfQueueing() {
    ControlSplashRs timer = {};
    control_splash_raise_rs(&timer, CONTROL_SPLASH_KIND_CHANGE, 0);
    TEST_ASSERT_TRUE(control_splash_poll_rs(&timer, 400));
    control_splash_raise_rs(&timer, CONTROL_SPLASH_KIND_CHANGE, 400);
    TEST_ASSERT_TRUE(control_splash_poll_rs(&timer, 1000));
    TEST_ASSERT_TRUE(control_splash_poll_rs(&timer, 400 + CONTROL_SPLASH_CHANGE_MS - 1));
    TEST_ASSERT_FALSE(control_splash_poll_rs(&timer, 400 + CONTROL_SPLASH_CHANGE_MS));
}

void test_aCriticalAlertPreemptsTheSplashAtOnce() {
    ControlSplashRs timer = {};
    control_splash_raise_rs(&timer, CONTROL_SPLASH_KIND_REFUSAL, 0);
    TEST_ASSERT_TRUE(control_splash_poll_rs(&timer, 10));
    TEST_ASSERT_TRUE(control_splash_preempt_rs(&timer));
    TEST_ASSERT_FALSE(control_splash_poll_rs(&timer, 11));
    TEST_ASSERT_FALSE(control_splash_preempt_rs(&timer));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_s01_antiLagEngaged);
    RUN_TEST(test_s02_antiLagOff);
    RUN_TEST(test_s03_launchArmed);
    RUN_TEST(test_s04_tractionLevelChanged);
    RUN_TEST(test_s05_ecuMapChanged);
    RUN_TEST(test_s06_refused);
    RUN_TEST(test_aStepperBackToZeroDisengages);
    RUN_TEST(test_aStaleGuardNeverInventsAReading);
    RUN_TEST(test_aHeldValueNeverRepeatsItselfUnderTheHero);
    RUN_TEST(test_theChangeWindowIs800msAndTheRefusal1200);
    RUN_TEST(test_aSecondChangeReplacesTheFirstInsteadOfQueueing);
    RUN_TEST(test_aCriticalAlertPreemptsTheSplashAtOnce);
    return UNITY_END();
}
