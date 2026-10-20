#include "usb_comm.h"

#include "can_scan_line.h"

#include <esp_system.h>
#include "usb_comm_internal.h"
#include "usb_seq_echo.h"
#include "usb_telemetry.h"

#include "app_config.h"
#include "board_config.h"
#include "diag/error_store.h"
#include "diag/heap_stats.h"
#include "diag/logger.h"
#include "util/mem_alloc.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>

#include <atomic>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

namespace {

constexpr uint8_t kCanScanDrainPerTick = 32;
constexpr size_t kCanScanLineCap = 96;

UsbComm::BurnOverlayShowCb s_burnOverlayShowCb = nullptr;
UsbComm::BurnOverlayShowErrorCb s_burnOverlayShowErrorCb = nullptr;

static_assert(kUsbRxBufSize >= kConfigJsonDocDashboard + 256,
              "USB_RX_BUF_SIZE must hold CONFIG_JSON_DOC_DASHBOARD + envelope overhead");

size_t s_rxPos = 0;

bool s_rxDraining = false;

uint8_t s_tickCount = 0;
constexpr uint8_t kTelePeriodTicks = 10;

struct CanHealthStats {
    uint32_t fpsX10;
    uint32_t errors;
    uint32_t missed;
};
CanHealthStats s_canStats = {0, 0, 0};
portMUX_TYPE s_canStatsMux = portMUX_INITIALIZER_UNLOCKED;
std::atomic<bool> s_canStatsPending{false};

void sendCanStatLine() {
    static constexpr size_t kCanStatBufWorstCase = 74;
    char statBuf[80];
    static_assert(sizeof(statBuf) >= kCanStatBufWorstCase,
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

void drainCanScanQueue() {
    UsbComm::CanScanFrame frame;
    uint8_t drained = 0;
    while (drained < kCanScanDrainPerTick && UsbCommInternal::canScanQueueTryReceive(frame)) {
        drained++;
        char line[kCanScanLineCap];
        if (CanScanLine::format(frame.id, frame.len, frame.data, line, sizeof(line)) == 0) {
            LOG_ERROR("USB", "can scan line truncated (id=%lu)",
                      static_cast<unsigned long>(frame.id));
            ErrorStore::push(ERROR_SRC_SYSTEM, "USB_TRUNC", "can scan line truncated");
            continue;
        }
        UsbComm::sendLine(line);
    }
}

}

namespace UsbCommInternal {
char *s_rxBuf = nullptr;
volatile uint32_t s_lastHostCmdMs = 0;
}

void UsbComm::reserveRxBuf() {
    if (UsbCommInternal::s_rxBuf) {
        return;
    }

    UsbCommInternal::s_rxBuf = static_cast<char *>(Mem::allocPreferSpiram(kUsbRxBufSize));
    if (!UsbCommInternal::s_rxBuf) {
        LOG_ERROR("USB", "rxBuf reserve (%u B) failed — USB receive disabled", kUsbRxBufSize);
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
    memset(UsbCommInternal::s_rxBuf, 0, kUsbRxBufSize);
    LOG_INFO("USB", "USB comm initialized");
}

namespace {

bool sendTagged(const char *line, size_t len, UsbSeqEcho::Seq seq) {
    char tagged[UsbSeqEcho::kMaxTaggedLine];
    const size_t taggedLen = UsbSeqEcho::tag(line, len, seq, tagged, sizeof(tagged));
    if (taggedLen == 0)
        return false;
    UsbCommInternal::routeLine(tagged, taggedLen);
    return true;
}

}

void UsbComm::sendLine(const char *line) {
    if (!line)
        return;
    const size_t len = strlen(line);
    const UsbSeqEcho::Seq seq = UsbSeqEcho::active();
    if (seq.present && sendTagged(line, len, seq))
        return;
    UsbCommInternal::routeLine(line, len);
}

void UsbComm::sendOk() {
    sendLine("{\"status\":\"ok\"}");
}

void UsbComm::sendOkRebooting() {
    sendLine("{\"status\":\"ok\",\"rebooting\":true}");
}

void UsbComm::flushIfHostActive() {
#ifdef ARDUINO
    if (isHostActive())
        Serial.flush();
#endif
}

void UsbComm::flushAndRestart() {
#ifdef ARDUINO
    flushIfHostActive();
    vTaskDelay(pdMS_TO_TICKS(kUsbPreRestartFlushDelayMs));
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
    constexpr uint32_t kHostTimeoutMs = 5000;
    if (UsbCommInternal::s_lastHostCmdMs == 0)
        return false;
    return (millis() - UsbCommInternal::s_lastHostCmdMs) < kHostTimeoutMs;
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

namespace {

void dropAbandonedLine() {
    if (s_rxPos == 0 || millis() - UsbCommInternal::s_lastHostCmdMs <= kUsbRxLineTimeoutMs)
        return;
    LOG_WARN("USB", "RX line abandoned after %u ms — dropping %u buffered byte(s)",
             static_cast<unsigned>(kUsbRxLineTimeoutMs), static_cast<unsigned>(s_rxPos));
    s_rxPos = 0;
    s_rxDraining = false;
}

void finishSerialLine() {
    UsbCommInternal::s_rxBuf[s_rxPos] = '\0';
    if (s_rxPos > 0)
        UsbCommInternal::dispatchFromSerial(UsbCommInternal::s_rxBuf);
    s_rxPos = 0;
}

void rejectOverlongLine() {
    LOG_WARN("USB", "RX buffer overflow (>%u B) — emitting error, draining to newline",
             static_cast<unsigned>(kUsbRxBufSize - 1));
    UsbComm::sendError("line_too_long");
    s_rxPos = 0;
    s_rxDraining = true;
}

void acceptSerialByte(char c) {
    UsbCommInternal::s_lastHostCmdMs = millis();
    if (s_rxDraining) {
        s_rxDraining = c != '\n';
        return;
    }
    if (c == '\n') {
        finishSerialLine();
        return;
    }
    if (s_rxPos >= kUsbRxBufSize - 1) {
        rejectOverlongLine();
        return;
    }
    UsbCommInternal::s_rxBuf[s_rxPos++] = c;
}

}

void UsbComm::tick() {
    if (!UsbCommInternal::s_rxBuf) {
        return;
    }

    UsbCommInternal::tickChunkTransferTimeout();
    const bool socketClosed = UsbCommInternal::takeSocketClosed();
    if (socketClosed)
        UsbCommInternal::abortSocketTransfers("tuner socket closed");
    UsbCommInternal::tickOtaSessionWatch(socketClosed);
    UsbCommInternal::publishSocketTransfer(UsbCommInternal::socketFileTransferOpen() ||
                                           UsbCommInternal::socketOtaSessionOpen());
    UsbCommInternal::tickObdDtc();

    dropAbandonedLine();
    while (Serial.available() > 0)
        acceptSerialByte(static_cast<char>(Serial.read()));

    drainCanScanQueue();

    if (s_canStatsPending.exchange(false, std::memory_order_acquire)) {
        sendCanStatLine();
    }

    if (++s_tickCount >= kTelePeriodTicks) {
        s_tickCount = 0;
        UsbTelemetry::sendLine();
    }

    HeapStats::tick();
}
