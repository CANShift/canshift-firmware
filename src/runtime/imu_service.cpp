#include "imu_service.h"

#include "app_config.h"
#include "config/board_profile_loader.h"
#include "config/config_loader.h"
#include "diag/error_store.h"
#include "diag/logger.h"
#include "hal/imu/qmi8658.h"
#include "hal/storage/nvs_store.h"
#include "imu_core_rs.h"
#include "runtime/imu_feed.h"
#include "runtime/signal_store.h"
#include "runtime/task_spawn.h"
#include "util/mem_alloc.h"

#include <Arduino.h>
#include <esp_task_wdt.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <new>

namespace ImuService {

namespace {

using canshift::boards::ImuDriver;
using canshift::boards::ImuProfile;

constexpr uint32_t kSamplePeriodMs = 10;
constexpr uint16_t kReadFailureReportAt = 50;
constexpr char kNvsNamespace[] = "imu";
constexpr char kNvsLevelKey[] = "level";

struct Sampler {
    ImuProfile imu;
    ImuCoreState core;
    ImuLevelCapture capture;
    LaunchTimer launch;
    bool capturing;
    uint16_t readFailures;
};

Mem::HeapPtr<Sampler> s_sampler;

void loadLevel(Sampler &s) {
    float gravity[3] = {};
    if (!NvsStore::getBytes(kNvsNamespace, kNvsLevelKey, gravity, sizeof(gravity))) {
        LOG_INFO("IMU", "not levelled yet — assuming an upright screen facing the driver");
        return;
    }
    if (!imu_core_level_rs(&s.core, gravity[0], gravity[1], gravity[2]))
        LOG_WARN("IMU", "stored level rejected — assuming an upright screen");
}

uint8_t applyLevel(Sampler &s) {
    float gravity[3] = {};
    const uint8_t status = imu_level_gravity_rs(&s.capture, gravity);
    if (status != kImuLevelOk)
        return status;
    if (!imu_core_level_rs(&s.core, gravity[0], gravity[1], gravity[2]))
        return kImuLevelScreenFlat;
    if (NvsStore::putBytes(kNvsNamespace, kNvsLevelKey, gravity, sizeof(gravity)))
        return kImuLevelOk;
    LOG_ERROR("IMU", "level applied but not saved");
    ErrorStore::push(ERROR_SRC_SYSTEM, "IMU_LEVEL", "level not saved");
    return ImuFeed::kLevelNotSaved;
}

void captureSample(Sampler &s, const float (&g)[3]) {
    if (imu_level_push_rs(&s.capture, &s.core, g[0], g[1], g[2]) == kImuLevelPending)
        return;
    s.capturing = false;
    const uint8_t status = applyLevel(s);
    LOG_INFO("IMU", "level capture finished with status %u", static_cast<unsigned>(status));
    ImuFeed::reportLevel(status);
}

void noteReadFailure(Sampler &s) {
    if (++s.readFailures != kReadFailureReportAt)
        return;
    LOG_ERROR("IMU", "%u accelerometer reads failed in a row", kReadFailureReportAt);
    ErrorStore::push(ERROR_SRC_SYSTEM, "IMU_READ", "accelerometer stopped answering");
}

void tickLaunch(Sampler &s) {
    const bool hasSpeed = SignalStore::isValid(SignalIds::kSpeedKph);
    const float speedKph = hasSpeed ? SignalStore::read(SignalIds::kSpeedKph) : 0.0f;
    const bool imperial = ConfigLoader::getDashboardConfig().units == CfgUnitSystem::IMPERIAL;
    imu_launch_tick_rs(&s.launch, millis(), s.core.longitudinal_g, hasSpeed, speedKph,
                       imu_launch_targets_rs(imperial));
}

void sample(Sampler &s) {
    float g[3] = {};
    if (!Qmi8658::readAccelG(s.imu, g)) {
        noteReadFailure(s);
        return;
    }
    s.readFailures = 0;
    if (ImuFeed::takeLevelRequest()) {
        s.capture = ImuLevelCapture{};
        s.capturing = true;
    }
    if (s.capturing)
        captureSample(s, g);
    if (ImuFeed::takePeakReset())
        imu_core_reset_peaks_rs(&s.core);
    imu_core_push_rs(&s.core, g[0], g[1], g[2]);
    tickLaunch(s);
    ImuFeed::publish(s.core, s.launch);
}

void taskImu(void *) {
    Sampler &s = *s_sampler;
    TickType_t lastWake = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(kSamplePeriodMs);
    while (true) {
        esp_task_wdt_reset();
        sample(s);
        vTaskDelayUntil(&lastWake, period);
    }
}

bool prepareCore(Sampler &s) {
    const uint8_t axisMap[3] = {static_cast<uint8_t>(s.imu.screen_x),
                                static_cast<uint8_t>(s.imu.screen_y),
                                static_cast<uint8_t>(s.imu.screen_z)};
    if (imu_core_init_rs(&s.core, axisMap))
        return true;
    LOG_ERROR("IMU", "board profile names an IMU axis twice");
    ErrorStore::push(ERROR_SRC_SYSTEM, "IMU_INIT", "imu axis map invalid");
    return false;
}

bool allocateSampler(const ImuProfile &imu) {
    void *raw = Mem::allocPreferSpiram(sizeof(Sampler));
    if (!raw || !ImuFeed::reserve()) {
        LOG_ERROR("IMU", "no memory for the IMU sampler");
        ErrorStore::push(ERROR_SRC_SYSTEM, "IMU_INIT", "imu sampler not allocated");
        Mem::HeapFree{}(raw);
        return false;
    }
    s_sampler.reset(new (raw) Sampler{imu, {}, {}, {}, false, 0});
    return true;
}

}

void init() {
    const ImuProfile &imu = canshift::boards::runtimeBoardProfile().imu;
    if (imu.driver == ImuDriver::None) {
        LOG_INFO("IMU", "board has no IMU — task not started");
        return;
    }
    if (!allocateSampler(imu) || !prepareCore(*s_sampler) || !Qmi8658::begin(imu))
        return;
    loadLevel(*s_sampler);
    if (TaskSpawn::spawnDynamic("imu", taskImu, TASK_STACK_IMU, kTaskPrioImu, kTaskCoreImu) ==
        nullptr)
        return;
    ImuFeed::markPresent();
    LOG_INFO("IMU", "QMI8658 sampled every %u ms", static_cast<unsigned>(kSamplePeriodMs));
}

}
