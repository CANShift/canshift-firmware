
#include "error_store.h"

#include "app_config.h"

#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>

#if USE_RUST_ERROR_STORE
    #include "error_store_rs.h"
#endif

static constexpr uint8_t RING_SIZE = ERROR_STORE_RING_SIZE;

static FwError s_ring[RING_SIZE];
static uint8_t s_head = 0;
static uint8_t s_count = 0;
static uint32_t s_version = 0;
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

void ErrorStore::push(ErrorSource source, const char *code, const char *message) {
    portENTER_CRITICAL(&s_mux);
#if USE_RUST_ERROR_STORE
    error_store_push_rs(s_ring, RING_SIZE, &s_head, &s_count, &s_version,
                        static_cast<uint8_t>(source), code, message);
#else
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

    uint8_t slot;
    if (s_count < RING_SIZE) {
        slot = (s_head + s_count) % RING_SIZE;
        s_count++;
    } else {
        slot = s_head;
        s_head = (s_head + 1) % RING_SIZE;
    }
    s_ring[slot].source = source;
    strncpy(s_ring[slot].code, code, sizeof(s_ring[slot].code) - 1);
    s_ring[slot].code[sizeof(s_ring[slot].code) - 1] = '\0';
    strncpy(s_ring[slot].message, message, sizeof(s_ring[slot].message) - 1);
    s_ring[slot].message[sizeof(s_ring[slot].message) - 1] = '\0';
    s_version++;
#endif
    portEXIT_CRITICAL(&s_mux);
}

void ErrorStore::getAll(FwError *buf, uint8_t *count, uint8_t maxCount) {
    portENTER_CRITICAL(&s_mux);
#if USE_RUST_ERROR_STORE
    *count = error_store_get_all_rs(s_ring, RING_SIZE, s_head, s_count, buf, maxCount);
#else
    uint8_t n = s_count < maxCount ? s_count : maxCount;
    for (uint8_t i = 0; i < n; i++) {
        uint8_t idx = (s_head + s_count - 1 - i) % RING_SIZE;
        buf[i] = s_ring[idx];
    }
    *count = n;
#endif
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

bool ErrorStore::peekLast(FwError *out) {
    portENTER_CRITICAL(&s_mux);
    if (s_count == 0) {
        portEXIT_CRITICAL(&s_mux);
        return false;
    }
    const uint8_t idx = static_cast<uint8_t>((s_head + s_count - 1) % RING_SIZE);
    *out = s_ring[idx];
    portEXIT_CRITICAL(&s_mux);
    return true;
}

void ErrorStore::dismissLatest() {
    portENTER_CRITICAL(&s_mux);
    if (s_count > 0) {
        s_count--;
        s_version++;
    }
    portEXIT_CRITICAL(&s_mux);
}

void ErrorStore::dismissAt(uint8_t row) {
    portENTER_CRITICAL(&s_mux);
#if USE_RUST_ERROR_STORE
    error_store_dismiss_at_rs(s_ring, RING_SIZE, &s_head, &s_count, &s_version, row);
#else
    if (row < s_count) {

        const uint8_t pos = static_cast<uint8_t>(s_count - 1 - row);
        if (pos == 0) {
            s_head = static_cast<uint8_t>((s_head + 1) % RING_SIZE);
        } else if (pos < s_count - 1) {
            for (uint8_t i = pos; i + 1 < s_count; i++) {
                const uint8_t dst = static_cast<uint8_t>((s_head + i) % RING_SIZE);
                const uint8_t src = static_cast<uint8_t>((s_head + i + 1) % RING_SIZE);
                s_ring[dst] = s_ring[src];
            }
        }
        s_count--;
        s_version++;
    }
#endif
    portEXIT_CRITICAL(&s_mux);
}

void ErrorStore::clear() {
    portENTER_CRITICAL(&s_mux);
    s_count = 0;
    s_head = 0;
    s_version++;
    portEXIT_CRITICAL(&s_mux);
}
