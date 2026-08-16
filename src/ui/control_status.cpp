#include "control_status.h"

#include "can/signal_map.h"
#include "control_state_rs.h"
#include "runtime/signal_store.h"

#include <math.h>

namespace ControlStatus {

namespace {

using ControlVocabulary::ControlId;
using ControlVocabulary::ControlKind;
using ControlVocabulary::ControlParam;
using ControlVocabulary::ControlState;

constexpr float kEgtHighC = 950.0f;
constexpr float kLaunchMaxSpeedKph = 3.0f;
constexpr float kPitMaxGear = 3.0f;
constexpr float kFlagOff = 0.0f;
constexpr SignalId kNoSignal = SignalIds::SIGNAL_COUNT;

struct Guard {
    SignalId signal;
    float threshold;
    bool blockWhenInvalid;
};

constexpr Guard kGuards[] = {
    {SignalIds::EGT_C, kEgtHighC, false},
    {SignalIds::SPEED_KPH, kFlagOff, true},
    {SignalIds::SPEED_KPH, kLaunchMaxSpeedKph, false},
    {SignalIds::GEAR, kPitMaxGear, false},
    {SignalIds::FLAG_BRAKE, kFlagOff, false},
    {SignalIds::MAP_NUMBER, kFlagOff, true},
};

constexpr SignalId kLevelSources[] = {kNoSignal, kNoSignal, kNoSignal,
                                      kNoSignal, kNoSignal, SignalIds::MAP_NUMBER};

static_assert(sizeof(kGuards) / sizeof(kGuards[0]) == static_cast<size_t>(ControlId::COUNT),
              "every ControlId needs one guard row");
static_assert(sizeof(kLevelSources) / sizeof(kLevelSources[0]) ==
                  static_cast<size_t>(ControlId::COUNT),
              "every ControlId needs one level source");

struct ParamSignal {
    ControlParam param;
    SignalId signal;
};

constexpr ParamSignal kParamSignals[] = {{ControlParam::RPM, SignalIds::RPM},
                                         {ControlParam::SPEED_KPH, SignalIds::SPEED_KPH},
                                         {ControlParam::GEAR, SignalIds::GEAR}};

bool signalValid(SignalId sid) {
    return sid < SignalIds::SIGNAL_COUNT && SignalStore::isValid(sid);
}

bool guardTripped(ControlId id) {
    const Guard &guard = kGuards[static_cast<uint8_t>(id)];
    const bool valid = signalValid(guard.signal);
    if (guard.blockWhenInvalid)
        return !valid;
    return valid && SignalStore::read(guard.signal, 0.0f) > guard.threshold;
}

int readRounded(SignalId sid) {
    if (!signalValid(sid))
        return 0;
    return static_cast<int>(lroundf(SignalStore::read(sid, 0.0f)));
}

} // namespace

bool isConfirming(const char *signalId) {
    if (!signalId || signalId[0] == '\0')
        return false;
    const SignalId sid = signalIdFromName(signalId);
    return signalValid(sid) && SignalStore::read(sid, 0.0f) != 0.0f;
}

ControlState evaluate(const ControlVocabulary::Control &control, const char *signalId,
                      const Request &request) {
    const bool blocked = guardTripped(control.id);
    const bool acting = isConfirming(signalId);
    const bool requested = control.kind == ControlKind::TOGGLE
                               ? request.engaged
                               : (control.stepFloor == 0 && request.level > 0);
    return ControlVocabulary::stateFromRaw(control_state_resolve_rs(
        blocked, acting, requested, ControlVocabulary::hasArmedState(control)));
}

int paramValue(ControlParam param, uint8_t level) {
    if (param == ControlParam::LEVEL)
        return level;
    for (const ParamSignal &row : kParamSignals) {
        if (row.param == param)
            return readRounded(row.signal);
    }
    return 0;
}

GuardReading guardReading(ControlId id) {
    const Guard &guard = kGuards[static_cast<uint8_t>(id)];
    const bool valid = signalValid(guard.signal);
    return {valid ? SignalStore::read(guard.signal, 0.0f) : 0.0f, guard.threshold, valid};
}

bool levelFromSignal(ControlId id, uint8_t *outLevel) {
    const SignalId sid = kLevelSources[static_cast<uint8_t>(id)];
    if (!outLevel || !signalValid(sid))
        return false;
    const int value = readRounded(sid);
    if (value < 0 || value > CONTROL_STEP_MAX)
        return false;
    *outLevel = static_cast<uint8_t>(value);
    return true;
}

} // namespace ControlStatus
