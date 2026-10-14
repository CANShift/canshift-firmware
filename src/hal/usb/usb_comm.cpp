#include "usb_comm.h"

#include <esp_system.h>
#include "usb_comm_internal.h"

#include "app_config.h"
#include "board_config.h"
#include "can/signal_map.h"
#include "diag/error_store.h"
#include "diag/heap_stats.h"
#include "diag/logger.h"
#include "runtime/signal_store.h"
#include "util/format_float.h"
#include "util/mem_alloc.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>

#include <atomic>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

namespace {

struct TeleEntry {
    SignalId id;
    const char *name;
};

const TeleEntry TELE_SIGNALS[] = {
    {SignalIds::RPM, "rpm"},
    {SignalIds::THROTTLE_POS, "throttle_pos"},
    {SignalIds::MAP_KPA, "map_kpa"},
    {SignalIds::BOOST_BAR, "boost_bar"},
    {SignalIds::IAT_C, "iat_c"},
    {SignalIds::COOLANT_TEMP_C, "coolant_temp_c"},
    {SignalIds::OIL_TEMP_C, "oil_temp_c"},
    {SignalIds::OIL_PRESS_BAR, "oil_press_bar"},
    {SignalIds::FUEL_PRESS_BAR, "fuel_press_bar"},
    {SignalIds::LAMBDA_1, "lambda_1"},
    {SignalIds::AFR_1, "afr_1"},
    {SignalIds::SPEED_KPH, "speed_kph"},
    {SignalIds::GEAR, "gear"},
    {SignalIds::BATTERY_VOLTS, "battery_volts"},
    {SignalIds::FLAG_MIL, "flag_mil"},
    {SignalIds::FLAG_LAUNCH_CTRL, "flag_launch_ctrl"},
    {SignalIds::MAP_NUMBER, "map_number"},
};

constexpr size_t TELE_SIGNAL_COUNT = sizeof(TELE_SIGNALS) / sizeof(TELE_SIGNALS[0]);

UsbComm::BurnOverlayShowCb s_burnOverlayShowCb = nullptr;
UsbComm::BurnOverlayShowErrorCb s_burnOverlayShowErrorCb = nullptr;

static_assert(USB_RX_BUF_SIZE >= CONFIG_JSON_DOC_DASHBOARD + 256,
              "USB_RX_BUF_SIZE must hold CONFIG_JSON_DOC_DASHBOARD + envelope overhead");

size_t s_rxPos = 0;

bool s_rxDraining = false;

uint8_t s_tickCount = 0;
constexpr uint8_t TELE_PERIOD_TICKS = 10;

struct CanHealthStats {
    uint32_t fpsX10;
    uint32_t errors;
    uint32_t missed;
};
CanHealthStats s_canStats = {0, 0, 0};
portMUX_TYPE s_canStatsMux = portMUX_INITIALIZER_UNLOCKED;
std::atomic<bool> s_canStatsPending{false};

constexpr size_t TELE_BUF_SIZE = 768;

void sendCanStatLine() {
    static constexpr size_t CAN_STAT_BUF_WORST_CASE = 74;
    char statBuf[80];
    static_assert(sizeof(statBuf) >= CAN_STAT_BUF_WORST_CASE,
                  "statBuf too small for worst-case can_stat payload");
    CanHealthStats stats;
    portENTER_CRITICAL(&s_canStatsMux);
    stats = s_canStats;
    portEXIT_CRITICAL(&s_canStatsMux);
    const int n = snprintf(statBuf, sizeof(statBuf),
                           "{\"can_stat\":1,\"fps\":%lu.%lu,\"errors\":%lu,\"missed\":%lu}\n",
                           static_cast<unsigned long>(stats.fpsX10 / 10),
                           static_cast<unsigned long>(stats.fpsX10 % 10),
                           static_cast<unsigned long>(stats.errors),
                           static_cast<unsigned long>(stats.missed));
    if (n > 0 && static_cast<size_t>(n) < sizeof(statBuf)) {
        UsbComm::sendLine(statBuf);
        return;
    }
    LOG_WARN("USB", "can_stat payload truncated (n=%d, cap=%u)", n,
             static_cast<unsigned>(sizeof(statBuf)));
    ErrorStore::push(ERROR_SRC_SYSTEM, "USB_TRUNC", "can_stat payload truncated");
}

void sendTelemetry() {
    char buf[TELE_BUF_SIZE];
    char *p = buf;
    const char *end = buf + TELE_BUF_SIZE - 2;

    const char *prefix = "{\"tele\":1,\"v\":{";
    size_t prefixLen = strlen(prefix);
    if (p + prefixLen >= end)
        return;
    memcpy(p, prefix, prefixLen);
    p += prefixLen;

    bool first = true;
    for (size_t i = 0; i < TELE_SIGNAL_COUNT; i++) {
        float val = 0.0f;
        if (!SignalStore::tryRead(TELE_SIGNALS[i].id, &val))
            continue;

        if (!first) {
            if (p >= end)
                break;
            *p++ = ',';
        }
        first = false;

        int n = snprintf(p, static_cast<size_t>(end - p), "\"%s\":", TELE_SIGNALS[i].name);
        if (n <= 0 || p + n >= end)
            break;
        p += n;
        char numBuf[16];
        size_t numLen = FloatFormat::formatGeneral(numBuf, sizeof(numBuf), val, 3);
        if (numLen == 0 || p + numLen >= end)
            break;
        memcpy(p, numBuf, numLen);
        p += numLen;
    }

    if (p + 3 <= end) {
        *p++ = '}';
        *p++ = '}';
        *p++ = '\n';
        *p = '\0';
        UsbComm::sendLine(buf);
    }
}

void drainCanScanQueue() {
    UsbComm::CanScanFrame frame;
    uint8_t drained = 0;
    while (drained < 32 && UsbCommInternal::canScanQueueTryReceive(frame)) {
        char buf[96];
        char *p = buf;
        const char *limit = buf + sizeof(buf) - 4;

        p += snprintf(p, static_cast<size_t>(limit - p), "{\"can\":1,\"id\":%lu,\"len\":%u,\"d\":[",
                      static_cast<unsigned long>(frame.id), static_cast<unsigned>(frame.len));

        for (uint8_t i = 0; i < frame.len && p < limit; i++) {
            if (i > 0 && p < limit)
                *p++ = ',';
            p += snprintf(p, static_cast<size_t>(limit - p), "%u",
                          static_cast<unsigned>(frame.data[i]));
        }

        if (p < limit)
            *p++ = ']';
        if (p < limit)
            *p++ = '}';
        if (p < limit)
            *p++ = '\n';
        *p = '\0';

        UsbComm::sendLine(buf);
        drained++;
    }
}

} // namespace

namespace UsbCommInternal {
char *s_rxBuf = nullptr;
volatile uint32_t s_lastHostCmdMs = 0;
} // namespace UsbCommInternal

void UsbComm::reserveRxBuf() {
    if (UsbCommInternal::s_rxBuf) {
        return;
    }

    UsbCommInternal::s_rxBuf = static_cast<char *>(Mem::allocPreferSpiram(USB_RX_BUF_SIZE));
    if (!UsbCommInternal::s_rxBuf) {

        LOG_ERROR("USB", "rxBuf reserve (%u B) failed — USB receive disabled", USB_RX_BUF_SIZE);
    }
}

void UsbComm::init() {
    s_rxPos = 0;
    s_rxDraining = false;
    s_tickCount = 0;

    if (!UsbCommInternal::s_rxBuf) {
        reserveRxBuf();
    }
    if (!UsbCommInternal::s_rxBuf) {
        return;
    }
    memset(UsbCommInternal::s_rxBuf, 0, USB_RX_BUF_SIZE);
    LOG_INFO("USB", "USB comm initialized");
}

void UsbComm::sendLine(const char *line) {
    if (!line)
        return;
    UsbCommInternal::routeLine(line, strlen(line));
}

void UsbComm::sendOk() {
    sendLine("{\"status\":\"ok\"}");
}

void UsbComm::sendOkRebooting() {
    sendLine("{\"status\":\"ok\",\"rebooting\":true}");
}

void UsbComm::flushAndRestart() {
#ifdef ARDUINO
    if (isHostActive())
        Serial.flush();
    vTaskDelay(pdMS_TO_TICKS(USB_PRE_RESTART_FLUSH_DELAY_MS));
    esp_restart();
#endif
}

void UsbComm::sendError(const char *code) {
    char buf[128];
    snprintf(buf, sizeof(buf), "{\"status\":\"error\",\"message\":\"%s\"}",
             code != nullptr ? code : "error");
    sendLine(buf);
}

void UsbComm::setBurnOverlayShowCallback(BurnOverlayShowCb cb) {
    s_burnOverlayShowCb = cb;
}

void UsbComm::setBurnOverlayShowErrorCallback(BurnOverlayShowErrorCb cb) {
    s_burnOverlayShowErrorCb = cb;
}

void UsbCommInternal::invokeBurnOverlayShow() {

    const UsbComm::BurnOverlayShowCb cb = s_burnOverlayShowCb;
    if (cb)
        cb();
}

void UsbCommInternal::invokeBurnOverlayShowError(int reason) {
    const UsbComm::BurnOverlayShowErrorCb cb = s_burnOverlayShowErrorCb;
    if (cb)
        cb(reason);
}

bool UsbComm::isHostActive() {
    constexpr uint32_t HOST_TIMEOUT_MS = 5000;
    if (UsbCommInternal::s_lastHostCmdMs == 0)
        return false;
    return (millis() - UsbCommInternal::s_lastHostCmdMs) < HOST_TIMEOUT_MS;
}

void UsbComm::updateCanStats(uint32_t fpsX10, uint32_t errors, uint32_t missed) {
    portENTER_CRITICAL(&s_canStatsMux);
    s_canStats.fpsX10 = fpsX10;
    s_canStats.errors = errors;
    s_canStats.missed = missed;
    portEXIT_CRITICAL(&s_canStatsMux);
    s_canStatsPending.store(true, std::memory_order_release);
}

bool UsbComm::pushCanFrame(const CanScanFrame &frame) {
    if (!UsbCommInternal::canScanModeActive())
        return false;

    return UsbCommInternal::canScanQueueTrySend(frame);
}

void UsbComm::tick() {

    if (!UsbCommInternal::s_rxBuf) {
        return;
    }

    UsbCommInternal::tickChunkTransferTimeout();
    UsbCommInternal::tickObdDtc();

    if (s_rxPos > 0 && (millis() - UsbCommInternal::s_lastHostCmdMs) > USB_RX_LINE_TIMEOUT_MS) {
        LOG_WARN("USB", "RX line abandoned after %u ms — dropping %u buffered byte(s)",
                 static_cast<unsigned>(USB_RX_LINE_TIMEOUT_MS), static_cast<unsigned>(s_rxPos));
        s_rxPos = 0;
        s_rxDraining = false;
    }

    while (Serial.available() > 0) {
        char c = static_cast<char>(Serial.read());

        UsbCommInternal::s_lastHostCmdMs = millis();

        if (s_rxDraining) {
            if (c == '\n') {
                s_rxDraining = false;
            }
            continue;
        }

        if (c == '\n') {
            UsbCommInternal::s_rxBuf[s_rxPos] = '\0';
            if (s_rxPos > 0) {
                UsbCommInternal::dispatchFromSerial(UsbCommInternal::s_rxBuf);
            }
            s_rxPos = 0;
        } else if (s_rxPos < USB_RX_BUF_SIZE - 1) {
            UsbCommInternal::s_rxBuf[s_rxPos++] = c;
        } else {

            LOG_WARN("USB", "RX buffer overflow (>%u B) — emitting error, draining to newline",
                     static_cast<unsigned>(USB_RX_BUF_SIZE - 1));
            UsbComm::sendError("line_too_long");
            s_rxPos = 0;
            s_rxDraining = true;
        }
    }

    drainCanScanQueue();

    if (s_canStatsPending.exchange(false, std::memory_order_acquire)) {
        sendCanStatLine();
    }

    if (++s_tickCount >= TELE_PERIOD_TICKS) {
        s_tickCount = 0;
        sendTelemetry();
    }

    HeapStats::tick();
}
