#ifndef CANSHIFT_BUS_SCAN_RS_H
#define CANSHIFT_BUS_SCAN_RS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

inline constexpr int kBusScanRateCount = 4;
inline constexpr int kBusScanIdCapacity = 128;
inline constexpr uint32_t kBusScanCollectMs = 3000;

#ifdef __cplusplus
extern "C" {
#endif

enum {
    kBusScanPhaseIdle = 0,
    kBusScanPhaseProbing = 1,
    kBusScanPhaseCollecting = 2,
    kBusScanPhaseDone = 3
};

enum {
    kBusScanOutcomeNone = 0,
    kBusScanOutcomeDetected = 1,
    kBusScanOutcomeSilent = 2,
    kBusScanOutcomeUnclear = 3,
    kBusScanOutcomeFailed = 4
};

enum { kBusScanActionNone = 0, kBusScanActionInstall = 1, kBusScanActionRestore = 2 };

typedef struct {
    uint32_t frames;
    uint32_t bus_errors;
    uint16_t kbps;
    uint16_t reserved;
} BusScanProbeRs;

typedef struct {
    uint32_t id;
    uint32_t count;
} BusScanSeenIdRs;

typedef struct {
    BusScanProbeRs probes[kBusScanRateCount];
    BusScanSeenIdRs ids[kBusScanIdCapacity];
    uint32_t phase_started_ms;
    uint16_t id_count;
    uint16_t install_kbps;
    uint8_t phase;
    uint8_t probe_index;
    uint8_t outcome;
    bool truncated;
} BusScanRs;

uint8_t bus_scan_begin_rs(BusScanRs *scan, uint32_t now_ms);
void bus_scan_on_frame_rs(BusScanRs *scan, uint32_t id);
uint8_t bus_scan_tick_rs(BusScanRs *scan, uint32_t now_ms, uint32_t bus_errors);
uint8_t bus_scan_fail_rs(BusScanRs *scan);
void bus_scan_abort_rs(BusScanRs *scan);
void bus_scan_queue_rs(BusScanRs *scan);

#ifdef __cplusplus
}

static_assert(sizeof(BusScanProbeRs) == 12, "BusScanProbeRs layout must match rust bus-scan Probe");
static_assert(sizeof(BusScanSeenIdRs) == 8, "BusScanSeenIdRs layout must match rust SeenId");
static_assert(sizeof(BusScanRs) == 1084, "BusScanRs layout must match rust bus-scan BusScan");
#endif

#endif
