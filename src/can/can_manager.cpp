// can_manager.cpp — TWAI hardware manager

#include "can_manager.h"
#include "maxxecu_parser.h"
#include "board_config.h"
#include "app_config.h"
#include "config/config_loader.h"
#include "diag/logger.h"
#include "diag/error_store.h"
#include "hal/usb/usb_comm.h"

#include <driver/twai.h>
#include <Arduino.h>
#include <string.h> // memcpy

// ---------------------------------------------------------------------------
// Internal state
// ---------------------------------------------------------------------------

static uint32_t s_frameCount = 0;
static uint32_t s_errorCount = 0;

// Health stats — frame rate window (reset every STAT_INTERVAL_MS)
static uint32_t s_windowFrames = 0;
static uint32_t s_lastStatMs = 0;
static constexpr uint32_t STAT_INTERVAL_MS = 2000;

// ---------------------------------------------------------------------------
// TWAI configuration helpers
// ---------------------------------------------------------------------------

namespace {

twai_timing_config_t getTimingConfig(uint16_t kbps) {
    switch (kbps) {
        case 1000: return TWAI_TIMING_CONFIG_1MBITS();
        case 500:  return TWAI_TIMING_CONFIG_500KBITS();
        case 250:  return TWAI_TIMING_CONFIG_250KBITS();
        default:
            LOG_WARN("CAN", "Unsupported canSpeedKbps=%d — falling back to %dkbps", kbps,
                     CAN_SPEED_KBPS);
            return TWAI_TIMING_CONFIG_500KBITS();
    }
}

twai_filter_config_t getFilterConfig() {
    // Accept all frames — CAN scanner mode requires full bus visibility and
    // signal IDs are user-configurable, so a fixed ID range filter would break both.
    return TWAI_FILTER_CONFIG_ACCEPT_ALL();
}

} // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void CanManager::initHardware() {
    const CfgDeviceConfig &dev = ConfigLoader::getDeviceConfig();

    // device.json overrides board_config.h for pins and speed
    const int txPin     = (dev.loaded && dev.twaiTxPin >= 0) ? dev.twaiTxPin : PIN_TWAI_TX;
    const int rxPin     = (dev.loaded && dev.twaiRxPin >= 0) ? dev.twaiRxPin : PIN_TWAI_RX;
    const uint16_t speedKbps = (dev.loaded && dev.canSpeedKbps > 0)
                              ? static_cast<uint16_t>(dev.canSpeedKbps)
                              : static_cast<uint16_t>(ConfigLoader::getSignalConfig().canSpeedKbps);

    LOG_INFO("CAN", "Initializing TWAI driver...");
    LOG_INFO("CAN", "TX=GPIO%d RX=GPIO%d speed=%dkbps", txPin, rxPin, speedKbps);

    twai_general_config_t g_config =
        TWAI_GENERAL_CONFIG_DEFAULT(static_cast<gpio_num_t>(txPin),
                                    static_cast<gpio_num_t>(rxPin), TWAI_MODE_NORMAL);
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
        return;
    }

    err = twai_start();
    if (err != ESP_OK) {
        LOG_ERROR("CAN", "TWAI start failed: %s", esp_err_to_name(err));
        char msg[52];
        snprintf(msg, sizeof(msg), "Start failed: %s", esp_err_to_name(err));
        ErrorStore::push(ERROR_SRC_CAN, "START_FAIL", msg);
        return;
    }

    // Load dynamic signal definitions from config
    MaxxEcuParser::loadSignalDefinitions();

    LOG_INFO("CAN", "TWAI driver started successfully");
}

void CanManager::tick() {
    twai_message_t message;

    // Block for up to 10ms waiting for a frame
    esp_err_t err = twai_receive(&message, pdMS_TO_TICKS(10));

    if (err == ESP_OK) {
        s_frameCount++;
        s_windowFrames++;

        if (!(message.rtr)) {
            // Data frame (not remote frame)
            MaxxEcuParser::parseFrame(message.identifier, message.data,
                                      static_cast<uint8_t>(message.data_length_code));

            // Forward raw frame to USB scan queue if scanner is active (best-effort, no lock)
            UsbComm::CanScanFrame sf;
            sf.id = message.identifier;
            sf.len = static_cast<uint8_t>(message.data_length_code);
            memcpy(sf.data, message.data, sf.len);
            UsbComm::pushCanFrame(sf);
        }
    } else if (err == ESP_ERR_TIMEOUT) {
        // Normal — no frame arrived within timeout window
        // This happens when ECU is not sending or CAN bus is quiet
    } else {
        s_errorCount++;
        LOG_WARN("CAN", "TWAI receive error: %s (total errors: %u)", esp_err_to_name(err),
                 s_errorCount);

        // Check for bus-off condition
        twai_status_info_t status;
        if (twai_get_status_info(&status) == ESP_OK) {
            if (status.state == TWAI_STATE_BUS_OFF) {
                LOG_ERROR("CAN", "TWAI bus-off — attempting recovery");
                twai_initiate_recovery();
                ErrorStore::push(ERROR_SRC_CAN, "BUS_OFF", "CAN bus-off, recovering");
            }
        }

        // Rate-limit generic error pushes (TWAI errors can storm on noisy bus)
        static uint32_t s_lastErrPushMs = 0;
        const uint32_t nowPush = millis();
        if (nowPush - s_lastErrPushMs >= 1000) {
            char msg[52];
            snprintf(msg, sizeof(msg), "%s (total: %lu)", esp_err_to_name(err),
                     static_cast<unsigned long>(s_errorCount));
            ErrorStore::push(ERROR_SRC_CAN, "TWAI_ERR", msg);
            s_lastErrPushMs = nowPush;
        }
    }

    // Emit health stats every STAT_INTERVAL_MS — reads millis() which is safe from any task
    const uint32_t nowMs = millis();
    if (s_lastStatMs == 0) {
        s_lastStatMs = nowMs;
    } else if (nowMs - s_lastStatMs >= STAT_INTERVAL_MS) {
        const uint32_t elapsed = nowMs - s_lastStatMs;
        const uint32_t fpsX10 = elapsed > 0 ? (s_windowFrames * 10000UL) / elapsed : 0;
        UsbComm::updateCanStats(fpsX10, s_errorCount);
        s_windowFrames = 0;
        s_lastStatMs = nowMs;
    }
}

uint32_t CanManager::getFrameCount() {
    return s_frameCount;
}
uint32_t CanManager::getErrorCount() {
    return s_errorCount;
}

bool CanManager::sendFrame(uint32_t id, const uint8_t *data, uint8_t len, bool extended) {
    // CAN classic frames carry at most 8 payload bytes — silently clamp to
    // protect callers from transmitting garbage past the end of `data`.
    static constexpr uint8_t kCanFrameMaxBytes = 8;
    if (len > kCanFrameMaxBytes)
        len = kCanFrameMaxBytes;

#if APP_SIMULATION_MODE
    // No TWAI driver in sim — log the would-be frame and report success so
    // UI click handlers don't treat every press as a failed send.
    LOG_INFO("CAN", "sim sendFrame id=0x%lX len=%u ext=%d",
             static_cast<unsigned long>(id), static_cast<unsigned>(len),
             extended ? 1 : 0);
    (void)data;
    return true;
#else
    twai_message_t msg = {};
    msg.identifier = id;
    msg.data_length_code = len;
    msg.extd = extended ? 1 : 0;
    if (len > 0 && data != nullptr) {
        memcpy(msg.data, data, len);
    }

    // Non-blocking — pass timeout=0 so a backed-up TX queue surfaces an error
    // rather than stalling the UI task that called us.
    esp_err_t err = twai_transmit(&msg, 0);
    if (err != ESP_OK) {
        LOG_WARN("CAN", "sendFrame failed id=0x%lX: %s",
                 static_cast<unsigned long>(id), esp_err_to_name(err));
        return false;
    }
    return true;
#endif
}
