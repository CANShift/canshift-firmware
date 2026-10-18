#include "sim_splash.h"

#include "can/signal_map.h"
#include "runtime/signal_store.h"
#include "ui/control_splash.h"
#include "ui/control_vocabulary.h"

#include <cstdio>
#include <cstring>

namespace SimSplash {

namespace {

using ControlVocabulary::ControlState;

constexpr uint32_t kFirstRaiseMs = 400;
constexpr uint32_t kReRaisePeriodMs = 300;

struct Case {
    const char *name;
    const char *control;
    ControlState state;
    uint8_t level;
    float egtC;
    float rpm;
    float speedKph;
    float gear;
};

constexpr Case kCases[] = {
    {"splash-s01", "ANTI-LAG", ControlState::ACTIVE, 0, 912.0f, 3200.0f, 0.0f, 0.0f},
    {"splash-s02", "ANTI-LAG", ControlState::OFF, 0, 906.0f, 3200.0f, 0.0f, 0.0f},
    {"splash-s03", "LAUNCH", ControlState::ARMED, 0, 700.0f, 4200.0f, 0.0f, 0.0f},
    {"splash-s04", "TRACTION", ControlState::ARMED, 4, 700.0f, 3000.0f, 60.0f, 3.0f},
    {"splash-s05", "ECU MAP", ControlState::OFF, 2, 700.0f, 2200.0f, 40.0f, 3.0f},
    {"splash-s06", "LAUNCH", ControlState::UNAVAILABLE, 0, 700.0f, 1200.0f, 24.0f, 2.0f},
};

const Case *s_case = nullptr;
const ControlVocabulary::Control *s_control = nullptr;
uint32_t s_startMs = 0;
uint32_t s_lastRaiseMs = 0;

void feed(const Case &c) {
    SignalStore::update(SignalIds::kEgtC, c.egtC);
    SignalStore::update(SignalIds::kRpm, c.rpm);
    SignalStore::update(SignalIds::kSpeedKph, c.speedKph);
    SignalStore::update(SignalIds::kGear, c.gear);
    SignalStore::update(SignalIds::kMapNumber, 1.0f);
    SignalStore::update(SignalIds::kFlagBrake, 0.0f);
    SignalStore::update(SignalIds::kCoolantTempC, 92.0f);
    SignalStore::update(SignalIds::kOilPressBar, 4.1f);
    SignalStore::update(SignalIds::kBatteryVolts, 13.8f);
}

}

bool select(const char *scenario, uint32_t nowMs) {
    if (!scenario)
        return false;
    for (const Case &entry : kCases) {
        if (strcmp(scenario, entry.name) != 0)
            continue;
        s_case = &entry;
        s_control = ControlVocabulary::find(entry.control);
        s_startMs = nowMs;
        s_lastRaiseMs = nowMs;
        printf("scenario: %s\n", entry.name);
        return true;
    }
    return false;
}

bool active() {
    return s_case != nullptr;
}

void tick(uint32_t nowMs) {
    if (!s_case)
        return;
    feed(*s_case);
    if (!s_control || nowMs - s_startMs < kFirstRaiseMs)
        return;
    if (nowMs - s_lastRaiseMs < kReRaisePeriodMs)
        return;
    s_lastRaiseMs = nowMs;
    ControlSplash::raiseFor(*s_control, s_case->state, s_case->level);
}

}
