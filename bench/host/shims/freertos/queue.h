#pragma once

#include "freertos/FreeRTOS.h"

#include <cstring>
#include <vector>

struct BenchQueue {
    std::vector<uint8_t> slots;
    UBaseType_t depth;
    UBaseType_t itemSize;
    UBaseType_t head;
    UBaseType_t count;
};

using QueueHandle_t = BenchQueue *;

inline QueueHandle_t xQueueCreate(UBaseType_t depth, UBaseType_t itemSize) {
    return new BenchQueue{std::vector<uint8_t>(depth * itemSize), depth, itemSize, 0, 0};
}

inline BaseType_t xQueueReset(QueueHandle_t queue) {
    queue->head = 0;
    queue->count = 0;
    return pdPASS;
}

inline BaseType_t xQueueSend(QueueHandle_t queue, const void *item, TickType_t) {
    if (queue->count == queue->depth)
        return pdFALSE;
    const UBaseType_t tail = (queue->head + queue->count) % queue->depth;
    memcpy(queue->slots.data() + tail * queue->itemSize, item, queue->itemSize);
    ++queue->count;
    return pdTRUE;
}

inline BaseType_t xQueueReceive(QueueHandle_t queue, void *out, TickType_t) {
    if (queue->count == 0)
        return pdFALSE;
    memcpy(out, queue->slots.data() + queue->head * queue->itemSize, queue->itemSize);
    queue->head = (queue->head + 1) % queue->depth;
    --queue->count;
    return pdTRUE;
}
