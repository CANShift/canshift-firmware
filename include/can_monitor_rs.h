#ifndef CANSHIFT_CAN_MONITOR_RS_H
#define CANSHIFT_CAN_MONITOR_RS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

inline constexpr int kCanMonitorIdCapacity = 64;
inline constexpr int kCanMonitorMaxFilters = 8;
inline constexpr int kCanMonitorMaxDataBytes = 8;
inline constexpr size_t kCanMonitorMaxFrameBytes = 244;
inline constexpr size_t kCanMonitorFilterHeaderBytes = 2;
inline constexpr size_t kCanMonitorFilterPairBytes = 8;
inline constexpr uint32_t kCanMonitorExtendedId = 0x80000000u;

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t id;
    uint32_t last_ms;
    uint16_t count;
    uint8_t dlc;
    bool dirty;
    uint8_t data[kCanMonitorMaxDataBytes];
} CanMonitorEntryRs;

typedef struct {
    uint32_t id;
    uint32_t mask;
} CanMonitorFilterRs;

typedef struct {
    CanMonitorEntryRs entries[kCanMonitorIdCapacity];
    CanMonitorFilterRs filters[kCanMonitorMaxFilters];
    uint16_t used;
    uint16_t cursor;
    uint8_t filter_count;
    bool overflow;
    uint8_t reserved[2];
} CanMonitorRs;

void can_monitor_reset_rs(CanMonitorRs *monitor);
void can_monitor_offer_rs(CanMonitorRs *monitor, uint32_t id, const uint8_t *data, uint8_t len,
                          uint32_t now_ms);
bool can_monitor_set_filter_rs(CanMonitorRs *monitor, const uint8_t *data, size_t len);
size_t can_monitor_encode_rs(CanMonitorRs *monitor, uint32_t now_ms, uint8_t *out, size_t cap);

#ifdef __cplusplus
}

static_assert(sizeof(CanMonitorEntryRs) == 20,
              "CanMonitorEntryRs layout must match rust MonitorEntry");
static_assert(sizeof(CanMonitorFilterRs) == 8,
              "CanMonitorFilterRs layout must match rust IdFilter");
static_assert(sizeof(CanMonitorRs) == 1352, "CanMonitorRs layout must match rust CanMonitor");
#endif

#endif
