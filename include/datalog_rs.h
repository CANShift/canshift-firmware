#ifndef CANSHIFT_DATALOG_RS_H
#define CANSHIFT_DATALOG_RS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

inline constexpr int kDatalogSignalSlots = 64;
inline constexpr size_t kDatalogMaxSampleBytes = 273;
inline constexpr size_t kDatalogMaxHeaderBytes = 2186;
inline constexpr size_t kDatalogMaxMarkerBytes = 15;
inline constexpr size_t kDatalogPreambleBytes = 5;

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t *storage;
    uint32_t capacity;
    uint32_t head;
    uint32_t tail;
    uint32_t dropped;
} DatalogRingRs;

size_t datalog_preamble_rs(uint8_t *out, size_t capacity);
size_t datalog_header_rs(uint8_t *out, size_t capacity, uint16_t sample_period_ms,
                         uint16_t session);
size_t datalog_sample_rs(uint8_t *out, size_t capacity, uint32_t t_ms, uint64_t mask,
                         const float *values);
size_t datalog_lap_rs(uint8_t *out, size_t capacity, uint32_t t_ms, uint16_t index,
                      uint32_t lap_ms);
size_t datalog_end_rs(uint8_t *out, size_t capacity, uint32_t t_ms, uint32_t dropped);

bool datalog_ring_init_rs(DatalogRingRs *ring, uint8_t *storage, uint32_t capacity);
bool datalog_ring_push_rs(const DatalogRingRs *ring, const uint8_t *bytes, size_t len);
size_t datalog_ring_pop_rs(const DatalogRingRs *ring, uint8_t *out, size_t capacity);
uint32_t datalog_ring_used_rs(const DatalogRingRs *ring);
uint32_t datalog_ring_dropped_rs(const DatalogRingRs *ring);

#ifdef __cplusplus
}

static_assert(sizeof(DatalogRingRs) == sizeof(uint8_t *) + 4 * sizeof(uint32_t),
              "DatalogRingRs layout must match rust datalog Ring");
#endif

#endif
