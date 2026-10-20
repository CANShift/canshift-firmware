#include "sim_imu.h"

#include "board_profile.h"
#include "imu_core_rs.h"
#include "runtime/imu_feed.h"

#include <Arduino.h>

#include <cmath>
#include <cstdio>
#include <cstring>

namespace SimImu {

namespace {

constexpr const char *kScenario = "gmeter";
using canshift::boards::ImuAxis;

constexpr uint8_t kUprightAxisMap[3] = {static_cast<uint8_t>(ImuAxis::PosX),
                                        static_cast<uint8_t>(ImuAxis::PosY),
                                        static_cast<uint8_t>(ImuAxis::PosZ)};
constexpr uint32_t kSamplePeriodMs = 10;
constexpr uint32_t kMaxSamples = 600;
constexpr float kSweepRadPerSample = 0.03f;
constexpr float kLateralPeakG = 0.9f;
constexpr float kLongitudinalPeakG = 0.7f;
constexpr float kGravityG = 1.0f;

bool s_active = false;
uint32_t s_startMs = 0;

void pushSample(ImuCoreState &core, uint32_t index) {
    const float angle = static_cast<float>(index) * kSweepRadPerSample;
    const float lateral = kLateralPeakG * sinf(angle);
    const float longitudinal = -kLongitudinalPeakG * cosf(angle);
    imu_core_push_rs(&core, lateral, kGravityG, -longitudinal);
}

}

void prepare(const char *scenario) {
    if (!scenario || strcmp(scenario, kScenario) != 0)
        return;
    if (!ImuFeed::reserve()) {
        printf("scenario: %s could not reserve the IMU snapshot\n", kScenario);
        return;
    }
    s_active = true;
    s_startMs = millis();
    ImuFeed::markPresent();
    printf("scenario: %s (synthetic IMU sweep)\n", kScenario);
}

void tick(uint32_t nowMs) {
    if (!s_active)
        return;
    const uint32_t elapsed = (nowMs - s_startMs) / kSamplePeriodMs;
    const uint32_t samples = elapsed < kMaxSamples ? elapsed : kMaxSamples;
    ImuCoreState core = {};
    if (!imu_core_init_rs(&core, kUprightAxisMap))
        return;
    for (uint32_t i = 0; i <= samples; ++i)
        pushSample(core, i);
    ImuFeed::publish(core, LaunchTimer{});
}

}
