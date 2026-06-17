#include "error_store.h"
#include "error_store_rs.h"

#include "app_config.h"

#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>

static constexpr uint8_t RING_SIZE = ERROR_STORE_RING_SIZE;

static FwError s_ring[RING_SIZE];
static uint8_t s_head = 0;
static uint8_t s_count = 0;
static uint32_t s_version = 0;
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

void ErrorStore::push(ErrorSource source, const char *code, const char *message) {
    portENTER_CRITICAL(&s_mux);
    error_store_push_rs(s_ring, RING_SIZE, &s_head, &s_count, &s_version,
                        static_cast<uint8_t>(source), code, message);
    portEXIT_CRITICAL(&s_mux);
}

void ErrorStore::getAll(FwError *buf, uint8_t *count, uint8_t maxCount) {
    portENTER_CRITICAL(&s_mux);
    *count = error_store_get_all_rs(s_ring, RING_SIZE, s_head, s_count, buf, maxCount);
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
    error_store_dismiss_at_rs(s_ring, RING_SIZE, &s_head, &s_count, &s_version, row);
    portEXIT_CRITICAL(&s_mux);
}

void ErrorStore::clear() {
    portENTER_CRITICAL(&s_mux);
    s_count = 0;
    s_head = 0;
    s_version++;
    portEXIT_CRITICAL(&s_mux);
}
