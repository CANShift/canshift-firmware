#include "usb_comm.h"
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

#ifdef ARDUINO

void serialSink(const char *data, size_t len) {
    if (!data || len == 0)
        return;
    const bool needsNewline = data[len - 1] != '\n';
    const bool locked = Logger::lockUart(pdMS_TO_TICKS(50));
    Serial.write(reinterpret_cast<const uint8_t *>(data), len);
    if (needsNewline)
        Serial.write('\n');
    if (locked)
        Logger::unlockUart();
}
#else
void serialSink(const char *, size_t) {}
#endif

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
};
CanHealthStats s_canStats = {0, 0};
portMUX_TYPE s_canStatsMux = portMUX_INITIALIZER_UNLOCKED;
std::atomic<bool> s_canStatsPending{false};

constexpr size_t TELE_BUF_SIZE = 768;

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

    SignalStore::SignalValue snap[SIGNAL_STORE_MAX_SIGNALS];
    SignalStore::snapshotAll(snap);

    bool first = true;
    for (size_t i = 0; i < TELE_SIGNAL_COUNT; i++) {
        const SignalId id = TELE_SIGNALS[i].id;
        if (!snap[id].valid)
            continue;
        float val = snap[id].smoothed;

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
    serialSink(line, strlen(line));
}

void UsbComm::sendOk() {
    sendLine("{\"status\":\"ok\"}");
}

void UsbComm::sendOkRebooting() {
    sendLine("{\"status\":\"ok\",\"rebooting\":true}");
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

void UsbComm::updateCanStats(uint32_t fpsX10, uint32_t errors) {
    portENTER_CRITICAL(&s_canStatsMux);
    s_canStats.fpsX10 = fpsX10;
    s_canStats.errors = errors;
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
                UsbCommInternal::handleCommand(UsbCommInternal::s_rxBuf);
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

        static constexpr size_t CAN_STAT_BUF_WORST_CASE = 54;
        static_assert(72 >= CAN_STAT_BUF_WORST_CASE,
                      "statBuf too small for worst-case can_stat payload");
        char statBuf[72];
        CanHealthStats stats;
        portENTER_CRITICAL(&s_canStatsMux);
        stats = s_canStats;
        portEXIT_CRITICAL(&s_canStatsMux);
        const int n =
            snprintf(statBuf, sizeof(statBuf), "{\"can_stat\":1,\"fps\":%lu.%lu,\"errors\":%lu}\n",
                     static_cast<unsigned long>(stats.fpsX10 / 10),
                     static_cast<unsigned long>(stats.fpsX10 % 10),
                     static_cast<unsigned long>(stats.errors));
        if (n > 0 && static_cast<size_t>(n) < sizeof(statBuf)) {
            UsbComm::sendLine(statBuf);
        } else {
            LOG_WARN("USB", "can_stat payload truncated (n=%d, cap=%u)", n,
                     static_cast<unsigned>(sizeof(statBuf)));
            ErrorStore::push(ERROR_SRC_SYSTEM, "USB_TRUNC", "can_stat payload truncated");
        }
    }

    if (++s_tickCount >= TELE_PERIOD_TICKS) {
        s_tickCount = 0;
        sendTelemetry();
    }

    HeapStats::tick();
}
