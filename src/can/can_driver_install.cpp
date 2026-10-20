#include "can_driver_install.h"
#include "can_parser.h"
#include "obd2_poller.h"
#include "board.h"
#include "app_config.h"
#include "config/config_loader.h"
#include "diag/logger.h"
#include "diag/error_store.h"

#include <driver/twai.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <atomic>
#include <new>
#include <stdio.h>

namespace {

constexpr uint32_t kTwaiInitTaskStack = 4096;
constexpr UBaseType_t kTwaiInitTaskPrio = 5;
constexpr uint32_t kTwaiInitTimeoutMs = 5000;
constexpr uint32_t kTwaiTxQueueLen = 5;
constexpr size_t kErrorMessageLen = 52;

StackType_t *s_initTaskStack = nullptr;
StaticTask_t s_initTaskTCB;

struct BusSetup {
    int txPin;
    int rxPin;
    uint16_t speedKbps;
};

struct InitContext {
    SemaphoreHandle_t done = nullptr;
    esp_err_t result = ESP_FAIL;
    std::atomic<bool> peerFinished{false};
    bool *installed = nullptr;
};

struct SpeedTiming {
    uint16_t kbps;
    twai_timing_config_t timing;
};

const SpeedTiming kSpeedTimings[] = {
    {1000, TWAI_TIMING_CONFIG_1MBITS()},
    {500, TWAI_TIMING_CONFIG_500KBITS()},
    {250, TWAI_TIMING_CONFIG_250KBITS()},
    {125, TWAI_TIMING_CONFIG_125KBITS()},
};

constexpr twai_mode_t kTwaiModes[] = {TWAI_MODE_NORMAL, TWAI_MODE_LISTEN_ONLY};

static_assert(sizeof(kTwaiModes) / sizeof(kTwaiModes[0]) ==
                  static_cast<size_t>(CanDriverInstall::BusMode::ListenOnly) + 1,
              "every bus mode needs a TWAI mode");

twai_timing_config_t getTimingConfig(uint16_t kbps) {
    for (const SpeedTiming &entry : kSpeedTimings) {
        if (entry.kbps == kbps)
            return entry.timing;
    }
    LOG_WARN("CAN", "Unsupported canSpeedKbps=%d — falling back to %dkbps", kbps,
             canshift::boards::runtimeBoardProfile().can.default_speed_kbps);
    return TWAI_TIMING_CONFIG_500KBITS();
}

BusSetup resolveBusSetup() {
    const CfgDeviceConfig &dev = ConfigLoader::getDeviceConfig();
    const auto &can = canshift::boards::runtimeBoardProfile().can;
    const bool txOverridden = dev.loaded && dev.twaiTxPin >= 0;
    const bool rxOverridden = dev.loaded && dev.twaiRxPin >= 0;
    const bool speedOverridden = dev.loaded && dev.canSpeedKbps > 0;
    return {
        txOverridden ? dev.twaiTxPin : can.pin_tx,
        rxOverridden ? dev.twaiRxPin : can.pin_rx,
        speedOverridden ? static_cast<uint16_t>(dev.canSpeedKbps)
                        : static_cast<uint16_t>(ConfigLoader::getSignalConfig().canSpeedKbps),
    };
}

esp_err_t reportStartError(const char *step, const char *storePrefix, const char *code,
                           esp_err_t err) {
    LOG_ERROR("CAN", "TWAI %s failed: %s", step, esp_err_to_name(err));
    char msg[kErrorMessageLen];
    snprintf(msg, sizeof(msg), "%s failed: %s", storePrefix, esp_err_to_name(err));
    ErrorStore::push(ERROR_SRC_CAN, code, msg);
    return err;
}

esp_err_t installDriver(const BusSetup &bus, CanDriverInstall::BusMode mode) {
    twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(
        static_cast<gpio_num_t>(bus.txPin), static_cast<gpio_num_t>(bus.rxPin),
        kTwaiModes[static_cast<size_t>(mode)]);
    g_config.rx_queue_len = kCanRxQueueDepth;
    g_config.tx_queue_len = kTwaiTxQueueLen;
    const twai_timing_config_t t_config = getTimingConfig(bus.speedKbps);
    const twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();
    const esp_err_t err = twai_driver_install(&g_config, &t_config, &f_config);
    if (err != ESP_OK)
        return reportStartError("driver install", "Init", "INIT_FAIL", err);
    return ESP_OK;
}

esp_err_t startDriver(const BusSetup &bus, CanDriverInstall::BusMode mode) {
    const esp_err_t installed = installDriver(bus, mode);
    if (installed != ESP_OK)
        return installed;
    const esp_err_t err = twai_start();
    if (err != ESP_OK)
        return reportStartError("start", "Start", "START_FAIL", err);
    return ESP_OK;
}

esp_err_t installAndStartOnThisCore() {
    const BusSetup bus = resolveBusSetup();
    LOG_INFO("CAN", "Initializing TWAI driver...");
    LOG_INFO("CAN", "TX=GPIO%d RX=GPIO%d speed=%dkbps", bus.txPin, bus.rxPin, bus.speedKbps);
    const esp_err_t err = startDriver(bus, CanDriverInstall::BusMode::Normal);
    if (err != ESP_OK)
        return err;
    CanParser::loadSignalDefinitions();
    Obd2Poller::init();
    LOG_INFO("CAN", "TWAI driver started successfully");
    return ESP_OK;
}

esp_err_t stopIfRunning() {
    const esp_err_t err = twai_stop();
    if (err == ESP_OK || err == ESP_ERR_INVALID_STATE)
        return ESP_OK;
    LOG_ERROR("CAN", "TWAI stop failed: %s", esp_err_to_name(err));
    return err;
}

void freeInitContext(InitContext *ctx) {
    vSemaphoreDelete(ctx->done);
    delete ctx;
}

void twaiInitTaskFn(void *arg) {
    auto *ctx = static_cast<InitContext *>(arg);
    ctx->result = installAndStartOnThisCore();
    LOG_INFO("CAN", "TWAI init task done on core %d (err=%s)", xPortGetCoreID(),
             esp_err_to_name(ctx->result));
    if (ctx->peerFinished.exchange(true)) {
        if (ctx->result == ESP_OK) {
            *ctx->installed = true;
        }
        freeInitContext(ctx);
    } else {
        xSemaphoreGive(ctx->done);
    }
    vTaskDelete(NULL);
}

InitContext *createInitContext(bool *installed) {
    auto *ctx = new (std::nothrow) InitContext();
    if (!ctx) {
        LOG_ERROR("CAN", "Failed to allocate init context");
        return nullptr;
    }
    ctx->installed = installed;
    ctx->done = xSemaphoreCreateBinary();
    if (!ctx->done) {
        delete ctx;
        LOG_ERROR("CAN", "Failed to create init semaphore");
        return nullptr;
    }
    return ctx;
}

TaskHandle_t spawnInitTask(InitContext *ctx) {
    if (s_initTaskStack) {
        return xTaskCreateStaticPinnedToCore(twaiInitTaskFn, "twai_init", kTwaiInitTaskStack, ctx,
                                             kTwaiInitTaskPrio, s_initTaskStack, &s_initTaskTCB, 0);
    }
    TaskHandle_t handle = nullptr;
    if (xTaskCreatePinnedToCore(twaiInitTaskFn, "twai_init", kTwaiInitTaskStack, ctx,
                                kTwaiInitTaskPrio, &handle, 0) != pdPASS) {
        return nullptr;
    }
    return handle;
}

void waitForGiveFromFinishedTask(InitContext *ctx) {
    xSemaphoreTake(ctx->done, portMAX_DELAY);
}

esp_err_t awaitInit(InitContext *ctx) {
    const bool signalled = xSemaphoreTake(ctx->done, pdMS_TO_TICKS(kTwaiInitTimeoutMs)) == pdTRUE;
    if (!signalled && !ctx->peerFinished.exchange(true)) {
        LOG_ERROR("CAN", "twai_init task timed out");
        return ESP_ERR_TIMEOUT;
    }
    if (!signalled)
        waitForGiveFromFinishedTask(ctx);
    const esp_err_t result = ctx->result;
    freeInitContext(ctx);
    return result;
}

}

void CanDriverInstall::reserveTaskStack() {
    if (s_initTaskStack) {
        return;
    }
    s_initTaskStack = static_cast<StackType_t *>(heap_caps_malloc(
        kTwaiInitTaskStack * sizeof(StackType_t), MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL));
    if (!s_initTaskStack) {
        LOG_ERROR("CAN", "Failed to reserve twai_init task stack (%u B)",
                  static_cast<unsigned>(kTwaiInitTaskStack * sizeof(StackType_t)));
    }
}

esp_err_t CanDriverInstall::installOnCore0(bool *installed) {
    InitContext *ctx = createInitContext(installed);
    if (!ctx)
        return ESP_ERR_NO_MEM;
    if (!spawnInitTask(ctx)) {
        freeInitContext(ctx);
        LOG_ERROR("CAN", "Failed to spawn twai_init task");
        return ESP_ERR_NO_MEM;
    }
    return awaitInit(ctx);
}

esp_err_t CanDriverInstall::startOnThisCore(BusMode mode, uint16_t speedKbps) {
    BusSetup bus = resolveBusSetup();
    bus.speedKbps = speedKbps;
    return startDriver(bus, mode);
}

esp_err_t CanDriverInstall::stopOnThisCore() {
    const esp_err_t stopped = stopIfRunning();
    if (stopped != ESP_OK)
        return stopped;
    const esp_err_t err = twai_driver_uninstall();
    if (err != ESP_OK)
        LOG_ERROR("CAN", "TWAI uninstall failed: %s", esp_err_to_name(err));
    return err;
}

uint16_t CanDriverInstall::configuredSpeedKbps() {
    return resolveBusSetup().speedKbps;
}
