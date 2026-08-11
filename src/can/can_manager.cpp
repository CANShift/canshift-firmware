#include "can_manager.h"
#include "can_parser.h"
#include "obd2_dtc.h"
#include "obd2_poller.h"
#include "board.h"
#include "app_config.h"
#include "config/config_loader.h"
#include "diag/logger.h"
#include "diag/error_store.h"
#include "hal/usb/usb_comm.h"

#include <driver/twai.h>
#include <esp_err.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <Arduino.h>
#include <atomic>
#include <new>
#include <string.h>

static uint32_t s_errorCount = 0;

static uint32_t s_windowFrames = 0;
static uint32_t s_lastStatMs = 0;
static uint32_t s_lastRxMs = 0;
static uint32_t s_lastBusRateX10 = 0;
static constexpr uint32_t STAT_INTERVAL_MS = 2000;

static bool s_twaiInstalled = false;
static uint32_t s_lastRetryMs = 0;
static uint8_t s_retryAttempts = 0;
static bool s_permanentlyDownWarned = false;

static constexpr uint32_t TWAI_INIT_TASK_STACK = 4096;
static constexpr UBaseType_t TWAI_INIT_TASK_PRIO = 5;
static constexpr uint32_t TWAI_INIT_TIMEOUT_MS = 5000;
static constexpr uint32_t TWAI_RX_ERROR_BACKOFF_MS = 5;
static constexpr uint32_t TWAI_RX_POLL_TIMEOUT_MS = 10;
static constexpr uint32_t TWAI_DOWN_TICK_DELAY_MS = 100;
static constexpr uint32_t RX_ERROR_REPORT_INTERVAL_MS = 1000;

static uint32_t s_lastErrLogMs = 0;
static uint32_t s_lastErrPushMs = 0;

static StackType_t *s_initTaskStack = nullptr;
static StaticTask_t s_initTaskTCB;

namespace {

twai_timing_config_t getTimingConfig(uint16_t kbps) {
    switch (kbps) {
        case 1000:
            return TWAI_TIMING_CONFIG_1MBITS();
        case 500:
            return TWAI_TIMING_CONFIG_500KBITS();
        case 250:
            return TWAI_TIMING_CONFIG_250KBITS();
        default:
            LOG_WARN("CAN", "Unsupported canSpeedKbps=%d — falling back to %dkbps", kbps,
                     kBoard.can.default_speed_kbps);
            return TWAI_TIMING_CONFIG_500KBITS();
    }
}

twai_filter_config_t getFilterConfig() {

    return TWAI_FILTER_CONFIG_ACCEPT_ALL();
}

esp_err_t installAndStartOnThisCore() {
    const CfgDeviceConfig &dev = ConfigLoader::getDeviceConfig();

    const int txPin = (dev.loaded && dev.twaiTxPin >= 0) ? dev.twaiTxPin : kBoard.can.pin_tx;
    const int rxPin = (dev.loaded && dev.twaiRxPin >= 0) ? dev.twaiRxPin : kBoard.can.pin_rx;
    const uint16_t speedKbps =
        (dev.loaded && dev.canSpeedKbps > 0)
            ? static_cast<uint16_t>(dev.canSpeedKbps)
            : static_cast<uint16_t>(ConfigLoader::getSignalConfig().canSpeedKbps);

    LOG_INFO("CAN", "Initializing TWAI driver...");
    LOG_INFO("CAN", "TX=GPIO%d RX=GPIO%d speed=%dkbps", txPin, rxPin, speedKbps);

    twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(
        static_cast<gpio_num_t>(txPin), static_cast<gpio_num_t>(rxPin), TWAI_MODE_NORMAL);
    g_config.rx_queue_len = CAN_RX_QUEUE_DEPTH;
    g_config.tx_queue_len = 5;

    twai_timing_config_t t_config = getTimingConfig(speedKbps);
    twai_filter_config_t f_config = getFilterConfig();

    esp_err_t err = twai_driver_install(&g_config, &t_config, &f_config);
    if (err != ESP_OK) {
        LOG_ERROR("CAN", "TWAI driver install failed: %s", esp_err_to_name(err));
        char msg[52];
        snprintf(msg, sizeof(msg), "Init failed: %s", esp_err_to_name(err));
        ErrorStore::push(ERROR_SRC_CAN, "INIT_FAIL", msg);
        return err;
    }

    err = twai_start();
    if (err != ESP_OK) {
        LOG_ERROR("CAN", "TWAI start failed: %s", esp_err_to_name(err));
        char msg[52];
        snprintf(msg, sizeof(msg), "Start failed: %s", esp_err_to_name(err));
        ErrorStore::push(ERROR_SRC_CAN, "START_FAIL", msg);
        return err;
    }

    CanParser::loadSignalDefinitions();

    Obd2Poller::init();

    LOG_INFO("CAN", "TWAI driver started successfully");
    return ESP_OK;
}

// Heap-allocated; the second side to flip handedOff owns freeing ctx and its semaphore.
struct InitContext {
    SemaphoreHandle_t done = nullptr;
    esp_err_t result = ESP_FAIL;
    std::atomic<bool> handedOff{false};
};

void freeInitContext(InitContext *ctx) {
    vSemaphoreDelete(ctx->done);
    delete ctx;
}

void twaiInitTaskFn(void *arg) {
    auto *ctx = static_cast<InitContext *>(arg);
    ctx->result = installAndStartOnThisCore();
    LOG_INFO("CAN", "TWAI init task done on core %d (err=%s)", xPortGetCoreID(),
             esp_err_to_name(ctx->result));
    if (ctx->handedOff.exchange(true)) {
        if (ctx->result == ESP_OK) {
            s_twaiInstalled = true;
        }
        freeInitContext(ctx);
    } else {
        xSemaphoreGive(ctx->done);
    }
    vTaskDelete(NULL);
}

} // namespace

bool CanManager::isAvailable() {
    return s_twaiInstalled;
}

uint32_t CanManager::msSinceLastRx() {
    if (s_lastRxMs == 0)
        return UINT32_MAX;
    return millis() - s_lastRxMs;
}

uint32_t CanManager::busRateHz() {
    return (s_lastBusRateX10 + 5) / 10;
}

void CanManager::reserveInitTaskStack() {
    if (s_initTaskStack) {
        return;
    }
    s_initTaskStack = static_cast<StackType_t *>(heap_caps_malloc(
        TWAI_INIT_TASK_STACK * sizeof(StackType_t), MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL));
    if (!s_initTaskStack) {
        LOG_ERROR("CAN", "Failed to reserve twai_init task stack (%u B)",
                  static_cast<unsigned>(TWAI_INIT_TASK_STACK * sizeof(StackType_t)));
    }
}

esp_err_t CanManager::initHardware() {
    auto *ctx = new (std::nothrow) InitContext();
    if (!ctx) {
        LOG_ERROR("CAN", "Failed to allocate init context");
        return ESP_ERR_NO_MEM;
    }
    ctx->done = xSemaphoreCreateBinary();
    if (!ctx->done) {
        delete ctx;
        LOG_ERROR("CAN", "Failed to create init semaphore");
        return ESP_ERR_NO_MEM;
    }
    TaskHandle_t handle = nullptr;
    if (s_initTaskStack) {

        handle =
            xTaskCreateStaticPinnedToCore(twaiInitTaskFn, "twai_init", TWAI_INIT_TASK_STACK, ctx,
                                          TWAI_INIT_TASK_PRIO, s_initTaskStack, &s_initTaskTCB, 0);
    } else {

        BaseType_t ok = xTaskCreatePinnedToCore(twaiInitTaskFn, "twai_init", TWAI_INIT_TASK_STACK,
                                                ctx, TWAI_INIT_TASK_PRIO, &handle, 0);
        if (ok != pdPASS) {
            handle = nullptr;
        }
    }
    if (!handle) {
        freeInitContext(ctx);
        LOG_ERROR("CAN", "Failed to spawn twai_init task");
        return ESP_ERR_NO_MEM;
    }
    if (xSemaphoreTake(ctx->done, pdMS_TO_TICKS(TWAI_INIT_TIMEOUT_MS)) != pdTRUE) {
        if (!ctx->handedOff.exchange(true)) {
            LOG_ERROR("CAN", "twai_init task timed out");
            return ESP_ERR_TIMEOUT;
        }
        // Task finished during the timeout race — its give is imminent.
        xSemaphoreTake(ctx->done, portMAX_DELAY);
    }
    const esp_err_t result = ctx->result;
    freeInitContext(ctx);
    if (result == ESP_OK) {
        s_twaiInstalled = true;
    }
    return result;
}

static void tickInitRetry() {
    const uint32_t nowMs = millis();
    const bool retryDue = s_retryAttempts < TWAI_INIT_MAX_RETRIES &&
                          (s_lastRetryMs == 0 || nowMs - s_lastRetryMs >= TWAI_INIT_RETRY_MS);
    if (retryDue) {
        s_retryAttempts++;
        s_lastRetryMs = nowMs;
        if (CanManager::initHardware() == ESP_OK) {
            LOG_INFO("CAN", "TWAI initialized after %u retries", s_retryAttempts);
        }
        return;
    }
    if (s_retryAttempts >= TWAI_INIT_MAX_RETRIES && !s_permanentlyDownWarned) {
        LOG_ERROR("CAN", "TWAI permanently down — heap too low at boot");
        ErrorStore::push(ERROR_SRC_CAN, "PERM_DOWN", "TWAI permanently down");
        s_permanentlyDownWarned = true;
    }
}

static void routeFrame(const twai_message_t &message) {
    s_windowFrames++;
    s_lastRxMs = millis();
    if (message.rtr)
        return;
    const uint8_t safeLen =
        static_cast<uint8_t>(message.data_length_code < kCanFrameMaxBytes ? message.data_length_code
                                                                          : kCanFrameMaxBytes);
    const bool consumedByPoller = Obd2Dtc::onRxFrame(message.identifier, message.data, safeLen);
    Obd2Poller::onRxFrame(message.identifier, message.data, safeLen);
    if (!consumedByPoller) {
        CanParser::parseFrame(message.identifier, message.data, safeLen);
    }
    UsbComm::CanScanFrame sf;
    sf.id = message.identifier;
    sf.len = safeLen;
    memcpy(sf.data, message.data, sf.len);
    UsbComm::pushCanFrame(sf);
}

static void maybeInitiateBusOffRecovery() {
    twai_status_info_t status;
    if (twai_get_status_info(&status) != ESP_OK || status.state != TWAI_STATE_BUS_OFF)
        return;
    LOG_ERROR("CAN", "TWAI bus-off — attempting recovery");
    ErrorStore::push(ERROR_SRC_CAN, "BUS_OFF", "CAN bus-off, recovering");
    const esp_err_t recoveryErr = twai_initiate_recovery();
    if (recoveryErr != ESP_OK) {
        LOG_ERROR("CAN", "recovery initiation failed: %s", esp_err_to_name(recoveryErr));
        ErrorStore::push(ERROR_SRC_CAN, "REC_FAIL", "bus-off recovery not started");
    }
}

static void handleRxError(esp_err_t err) {
    s_errorCount++;
    const uint32_t nowMs = millis();
    if (nowMs - s_lastErrLogMs >= RX_ERROR_REPORT_INTERVAL_MS) {
        LOG_WARN("CAN", "TWAI receive error: %s (total errors: %u)", esp_err_to_name(err),
                 s_errorCount);
        s_lastErrLogMs = nowMs;
    }
    maybeInitiateBusOffRecovery();
    if (nowMs - s_lastErrPushMs >= RX_ERROR_REPORT_INTERVAL_MS) {
        char msg[52];
        snprintf(msg, sizeof(msg), "%s (total: %lu)", esp_err_to_name(err),
                 static_cast<unsigned long>(s_errorCount));
        ErrorStore::push(ERROR_SRC_CAN, "TWAI_ERR", msg);
        s_lastErrPushMs = nowMs;
    }

    // Hard errors return immediately (no rx-timeout block) — without a
    // backoff this priority-15 task starves core 0 during bus-off recovery.
    vTaskDelay(pdMS_TO_TICKS(TWAI_RX_ERROR_BACKOFF_MS));
}

static void updateStatsWindow(uint32_t nowMs) {
    if (s_lastStatMs == 0) {
        s_lastStatMs = nowMs;
        return;
    }
    if (nowMs - s_lastStatMs < STAT_INTERVAL_MS)
        return;
    const uint32_t elapsed = nowMs - s_lastStatMs;
    const uint32_t fpsX10 = elapsed > 0 ? (s_windowFrames * 10000UL) / elapsed : 0;
    s_lastBusRateX10 = fpsX10;
    UsbComm::updateCanStats(fpsX10, s_errorCount);
    s_windowFrames = 0;
    s_lastStatMs = nowMs;
}

bool CanManager::tick() {
    if (!s_twaiInstalled) {
        tickInitRetry();
        vTaskDelay(pdMS_TO_TICKS(TWAI_DOWN_TICK_DELAY_MS));
        return false;
    }

    twai_message_t message;
    const esp_err_t err = twai_receive(&message, pdMS_TO_TICKS(TWAI_RX_POLL_TIMEOUT_MS));
    if (err == ESP_OK) {
        routeFrame(message);
    } else if (err != ESP_ERR_TIMEOUT) {
        handleRxError(err);
    }

    const uint32_t nowMs = millis();
    updateStatsWindow(nowMs);
    Obd2Poller::tick(nowMs);

    return err == ESP_OK;
}

bool CanManager::sendFrame(uint32_t id, const uint8_t *data, uint8_t len, bool extended) {

    if (!s_twaiInstalled)
        return false;

    if (len > kCanFrameMaxBytes)
        len = kCanFrameMaxBytes;

    twai_message_t msg = {};
    msg.identifier = id;
    msg.data_length_code = len;
    msg.extd = extended ? 1 : 0;
    if (len > 0 && data != nullptr) {
        memcpy(msg.data, data, len);
    }

    esp_err_t err = twai_transmit(&msg, 0);
    if (err != ESP_OK) {
        LOG_WARN("CAN", "sendFrame failed id=0x%lX: %s", static_cast<unsigned long>(id),
                 esp_err_to_name(err));
        return false;
    }
    return true;
}
