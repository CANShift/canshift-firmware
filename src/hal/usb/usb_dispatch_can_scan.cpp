#include "usb_comm_internal.h"

#include "app_config.h"
#include "diag/error_store.h"
#include "diag/logger.h"

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include <atomic>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

namespace {

constexpr uint8_t CAN_SCAN_QUEUE_DEPTH = 64;
// Created once on first scan start and never deleted (only reset), so a
// producer holding a stale handle can never touch freed queue memory.
// s_canScanQueue is null while scanning is stopped; s_canScanQueueStorage
// keeps the handle for reuse.
QueueHandle_t s_canScanQueueStorage = nullptr;
std::atomic<QueueHandle_t> s_canScanQueue{nullptr};
std::atomic<bool> s_canScanMode{false};

uint32_t s_scanDrops = 0;

} // namespace

namespace UsbCommInternal {

bool canScanModeActive() {
    return s_canScanMode.load(std::memory_order_acquire);
}

bool canScanQueueTrySend(const UsbComm::CanScanFrame &frame) {
    QueueHandle_t queue = s_canScanQueue.load(std::memory_order_acquire);
    if (queue == nullptr)
        return false;
    if (xQueueSend(queue, &frame, 0) != pdTRUE) {
        s_scanDrops++;
        return false;
    }
    return true;
}

bool canScanQueueTryReceive(UsbComm::CanScanFrame &out) {
    QueueHandle_t queue = s_canScanQueue.load(std::memory_order_acquire);
    if (queue == nullptr)
        return false;
    return xQueueReceive(queue, &out, 0) == pdTRUE;
}

void handleCanScanStart() {
    s_scanDrops = 0;

    if (!s_canScanQueueStorage) {
        s_canScanQueueStorage = xQueueCreate(CAN_SCAN_QUEUE_DEPTH, sizeof(UsbComm::CanScanFrame));
        if (!s_canScanQueueStorage) {
            LOG_ERROR("USB", "CAN scan start: queue alloc failed");
#if APP_USB_CAN_SCAN_FAIL_LOUD

            abort();
#endif

            ErrorStore::push(ERROR_SRC_SYSTEM, "SCAN_QUEUE", "CAN scan queue alloc failed");
            UsbComm::sendLine("{\"status\":\"error\",\"error\":\"queue_alloc_failed\"}");
            return;
        }
    }

    xQueueReset(s_canScanQueueStorage);
    s_canScanQueue.store(s_canScanQueueStorage, std::memory_order_release);
    s_canScanMode.store(true, std::memory_order_release);
    LOG_INFO("USB", "CAN scan started");
    UsbComm::sendOk();
}

void handleCanScanStop() {
    s_canScanMode.store(false, std::memory_order_release);
    s_canScanQueue.store(nullptr, std::memory_order_release);
    LOG_INFO("USB", "CAN scan stopped — drops: %lu", (unsigned long)s_scanDrops);
    char stopResp[64];
    const int stopN = snprintf(stopResp, sizeof(stopResp), "{\"status\":\"ok\",\"drops\":%lu}",
                               (unsigned long)s_scanDrops);
    if (stopN <= 0 || static_cast<size_t>(stopN) >= sizeof(stopResp)) {
        LOG_WARN("USB", "STOP_CAN_SCAN payload truncated (n=%d, cap=%u)", stopN,
                 static_cast<unsigned>(sizeof(stopResp)));
        return;
    }
    UsbComm::sendLine(stopResp);
}

} // namespace UsbCommInternal
