// error_store.cpp — Thread-safe firmware error ring buffer

#include "error_store.h"

#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>

// ---------------------------------------------------------------------------
// Internal state
// ---------------------------------------------------------------------------

static constexpr uint8_t RING_SIZE = 6;

static FwError s_ring[RING_SIZE];
static uint8_t s_head = 0; // Index of the oldest error
static uint8_t s_count = 0;
static uint32_t s_version = 0;
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void ErrorStore::push(ErrorSource source, const char *code, const char *message) {
    portENTER_CRITICAL(&s_mux);

    // If same source+code already in ring, update message in-place
    for (uint8_t i = 0; i < s_count; i++) {
        uint8_t idx = (s_head + i) % RING_SIZE;
        if (s_ring[idx].source == source &&
            strncmp(s_ring[idx].code, code, sizeof(s_ring[idx].code)) == 0) {
            strncpy(s_ring[idx].message, message, sizeof(s_ring[idx].message) - 1);
            s_ring[idx].message[sizeof(s_ring[idx].message) - 1] = '\0';
            s_version++;
            portEXIT_CRITICAL(&s_mux);
            return;
        }
    }

    // New error — write to next available slot
    uint8_t slot;
    if (s_count < RING_SIZE) {
        slot = (s_head + s_count) % RING_SIZE;
        s_count++;
    } else {
        // Ring full — overwrite oldest
        slot = s_head;
        s_head = (s_head + 1) % RING_SIZE;
    }
    s_ring[slot].source = source;
    strncpy(s_ring[slot].code, code, sizeof(s_ring[slot].code) - 1);
    s_ring[slot].code[sizeof(s_ring[slot].code) - 1] = '\0';
    strncpy(s_ring[slot].message, message, sizeof(s_ring[slot].message) - 1);
    s_ring[slot].message[sizeof(s_ring[slot].message) - 1] = '\0';
    s_version++;

    portEXIT_CRITICAL(&s_mux);
}

void ErrorStore::getAll(FwError *buf, uint8_t *count, uint8_t maxCount) {
    portENTER_CRITICAL(&s_mux);
    uint8_t n = s_count < maxCount ? s_count : maxCount;
    // Copy newest-first: index (head + count - 1) down to head
    for (uint8_t i = 0; i < n; i++) {
        uint8_t idx = (s_head + s_count - 1 - i) % RING_SIZE;
        buf[i] = s_ring[idx];
    }
    *count = n;
    portEXIT_CRITICAL(&s_mux);
}

uint8_t ErrorStore::getCount() {
    portENTER_CRITICAL(&s_mux);
    uint8_t c = s_count;
    portEXIT_CRITICAL(&s_mux);
    return c;
}

uint32_t ErrorStore::getVersion() {
    portENTER_CRITICAL(&s_mux);
    uint32_t v = s_version;
    portEXIT_CRITICAL(&s_mux);
    return v;
}

void ErrorStore::dismissLatest() {
    portENTER_CRITICAL(&s_mux);
    if (s_count > 0) {
        s_count--;
        s_version++;
    }
    portEXIT_CRITICAL(&s_mux);
}

void ErrorStore::clear() {
    portENTER_CRITICAL(&s_mux);
    s_count = 0;
    s_head = 0;
    s_version++;
    portEXIT_CRITICAL(&s_mux);
}
