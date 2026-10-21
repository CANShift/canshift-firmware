#include "can_manager.h"
#include "bus_scan.h"
#include "can_monitor.h"
#include "can_driver_install.h"
#include "can_parser_rs.h"
#include "can_rx_route.h"
#include "obd2_poller.h"
#include "app_config.h"
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
#include <string.h>

static uint32_t s_errorCount = 0;

static uint32_t s_windowFrames = 0;
static uint32_t s_lastStatMs = 0;
static std::atomic<uint32_t> s_lastRxMs{0};
static uint32_t s_lastBusRateX10 = 0;
static constexpr uint32_t kStatIntervalMs = 2000;

static bool s_twaiInstalled = false;
static uint32_t s_lastRetryMs = 0;
static uint8_t s_retryAttempts = 0;
static bool s_permanentlyDownWarned = false;
static uint8_t s_busOffRecoveries = 0;

static constexpr uint32_t kTwaiRxErrorBackoffMs = 5;
static constexpr uint32_t kTwaiRxPollTimeoutMs = 10;
static constexpr uint32_t kTwaiDownTickDelayMs = 100;
static constexpr uint32_t kRxErrorReportIntervalMs = 1000;

static uint32_t s_lastErrLogMs = 0;
static uint32_t s_lastErrPushMs = 0;
static uint32_t s_rxDropped = 0;
static bool s_rxDropPushed = false;

bool CanManager::isAvailable() {
    return s_twaiInstalled;
}

uint32_t CanManager::msSinceLastRx() {
    const uint32_t lastRxMs = s_lastRxMs.load(std::memory_order_relaxed);
    if (lastRxMs == 0)
        return UINT32_MAX;
    return millis() - lastRxMs;
}

uint32_t CanManager::busRateHz() {
    return (s_lastBusRateX10 + 5) / 10;
}

void CanManager::reserveInitTaskStack() {
    CanDriverInstall::reserveTaskStack();
}

esp_err_t CanManager::initHardware() {
    const esp_err_t result = CanDriverInstall::installOnCore0(&s_twaiInstalled);
    if (result == ESP_OK) {
        s_twaiInstalled = true;
    }
    return result;
}

static void tickInitRetry() {
    const uint32_t nowMs = millis();
    const bool retryDue = s_retryAttempts < kTwaiInitMaxRetries &&
                          (s_lastRetryMs == 0 || nowMs - s_lastRetryMs >= kTwaiInitRetryMs);
    if (retryDue) {
        s_retryAttempts++;
        s_lastRetryMs = nowMs;
        if (CanManager::initHardware() == ESP_OK) {
            LOG_INFO("CAN", "TWAI initialized after %u retries", s_retryAttempts);
        }
        return;
    }
    if (s_retryAttempts >= kTwaiInitMaxRetries && !s_permanentlyDownWarned) {
        LOG_ERROR("CAN", "TWAI permanently down — heap too low at boot");
        ErrorStore::push(ERROR_SRC_CAN, "PERM_DOWN", "TWAI permanently down");
        s_permanentlyDownWarned = true;
    }
}

static void routeFrame(const twai_message_t &message) {
    const uint32_t nowMs = millis();
    s_windowFrames++;
    s_lastRxMs.store(nowMs, std::memory_order_relaxed);
    if (message.rtr)
        return;
    const uint8_t safeLen =
        static_cast<uint8_t>(message.data_length_code < kCanFrameMaxBytes ? message.data_length_code
                                                                          : kCanFrameMaxBytes);
    CanRxRoute::deliver(message.identifier, message.data, safeLen);
    CanMonitor::offer(message.identifier, message.extd != 0, message.data, safeLen, nowMs);
    UsbComm::CanScanFrame sf;
    sf.id = message.identifier;
    sf.len = safeLen;
    memcpy(sf.data, message.data, sf.len);
    UsbComm::pushCanFrame(sf);
}

static void yieldCoreAfterHardRxError() {
    vTaskDelay(pdMS_TO_TICKS(kTwaiRxErrorBackoffMs));
}

static void handleRxError(esp_err_t err) {
    s_errorCount++;
    const uint32_t nowMs = millis();
    if (nowMs - s_lastErrLogMs >= kRxErrorReportIntervalMs) {
        LOG_WARN("CAN", "TWAI receive error: %s (total errors: %u)", esp_err_to_name(err),
                 s_errorCount);
        s_lastErrLogMs = nowMs;
    }
    if (nowMs - s_lastErrPushMs >= kRxErrorReportIntervalMs) {
        char msg[52];
        snprintf(msg, sizeof(msg), "%s (total: %lu)", esp_err_to_name(err),
                 static_cast<unsigned long>(s_errorCount));
        ErrorStore::push(ERROR_SRC_CAN, "TWAI_ERR", msg);
        s_lastErrPushMs = nowMs;
    }
    yieldCoreAfterHardRxError();
}

static void reportRxDrops(const twai_status_info_t &status) {
    const uint32_t dropped = status.rx_missed_count + status.rx_overrun_count;
    const uint32_t previous = s_rxDropped;
    s_rxDropped = dropped;
    if (dropped <= previous)
        return;
    LOG_WARN("CAN", "TWAI lost %lu frames (total %lu) — RX queue full or FIFO overrun",
             static_cast<unsigned long>(dropped - previous), static_cast<unsigned long>(dropped));
    if (s_rxDropPushed)
        return;
    s_rxDropPushed = true;
    ErrorStore::push(ERROR_SRC_CAN, "RX_DROPPED", "frames lost: RX queue or FIFO overrun");
}

static_assert(TWAI_STATE_STOPPED == kCanBusStateStopped, "TWAI stopped state drifted");
static_assert(TWAI_STATE_RUNNING == kCanBusStateRunning, "TWAI running state drifted");
static_assert(TWAI_STATE_BUS_OFF == kCanBusStateBusOff, "TWAI bus-off state drifted");
static_assert(TWAI_STATE_RECOVERING == kCanBusStateRecovering, "TWAI recovering state drifted");

static void leaveBusAlone() {}

static void recoverFromBusOff() {
    ++s_busOffRecoveries;
    LOG_ERROR("CAN", "TWAI bus-off — attempting recovery %u",
              static_cast<unsigned>(s_busOffRecoveries));
    ErrorStore::push(ERROR_SRC_CAN, "BUS_OFF", "CAN bus-off, recovering");
    const esp_err_t err = twai_initiate_recovery();
    if (err != ESP_OK) {
        LOG_ERROR("CAN", "recovery initiation failed: %s", esp_err_to_name(err));
        ErrorStore::push(ERROR_SRC_CAN, "REC_FAIL", "bus-off recovery not started");
    }
}

static void restartAfterRecovery() {
    const esp_err_t err = twai_start();
    if (err != ESP_OK) {
        LOG_ERROR("CAN", "TWAI restart after recovery failed: %s", esp_err_to_name(err));
        ErrorStore::push(ERROR_SRC_CAN, "RESTART_ERR", "bus-off recovered, restart failed");
        return;
    }
    LOG_INFO("CAN", "TWAI recovered from bus-off and restarted");
}

static void giveUpOnBus() {
    ++s_busOffRecoveries;
    LOG_ERROR("CAN", "TWAI bus-off again — recovery stopped until reboot");
    ErrorStore::push(ERROR_SRC_CAN, "BUS_DOWN", "CAN bus-off persists, recovery stopped");
}

using BusActionHandler = void (*)();

static constexpr BusActionHandler kBusActionHandlers[] = {
    leaveBusAlone,
    recoverFromBusOff,
    restartAfterRecovery,
    giveUpOnBus,
};

static_assert(sizeof(kBusActionHandlers) / sizeof(kBusActionHandlers[0]) == kCanBusActionGiveUp + 1,
              "every CAN bus action needs a handler");

static void superviseBus() {
    twai_status_info_t status;
    const esp_err_t err = twai_get_status_info(&status);
    if (err != ESP_OK) {
        LOG_WARN("CAN", "TWAI status read failed: %s", esp_err_to_name(err));
        return;
    }
    reportRxDrops(status);
    const uint8_t action =
        can_bus_action_rs(static_cast<uint8_t>(status.state), s_busOffRecoveries);
    if (action > kCanBusActionGiveUp)
        return;
    kBusActionHandlers[action]();
}

static void updateStatsWindow(uint32_t nowMs) {
    if (s_lastStatMs == 0) {
        s_lastStatMs = nowMs;
        return;
    }
    if (nowMs - s_lastStatMs < kStatIntervalMs)
        return;
    const uint32_t elapsed = nowMs - s_lastStatMs;
    const uint32_t fpsX10 = elapsed > 0 ? (s_windowFrames * 10000UL) / elapsed : 0;
    s_lastBusRateX10 = fpsX10;
    superviseBus();
    UsbComm::updateCanStats(fpsX10, s_errorCount, s_rxDropped);
    s_windowFrames = 0;
    s_lastStatMs = nowMs;
}

static void markDriverDown() {
    s_twaiInstalled = false;
    s_retryAttempts = 0;
    s_lastRetryMs = 0;
}

static bool tickBusScan() {
    const BusScan::Tick tick = BusScan::tick();
    if (tick == BusScan::Tick::DriverLost)
        markDriverDown();
    return tick == BusScan::Tick::Frame;
}

bool CanManager::tick() {
    if (s_twaiInstalled && BusScan::claimsBus())
        return tickBusScan();
    if (!s_twaiInstalled) {
        tickInitRetry();
        vTaskDelay(pdMS_TO_TICKS(kTwaiDownTickDelayMs));
        return false;
    }

    twai_message_t message;
    const esp_err_t err = twai_receive(&message, pdMS_TO_TICKS(kTwaiRxPollTimeoutMs));
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
    if (!s_twaiInstalled || BusScan::claimsBus())
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
