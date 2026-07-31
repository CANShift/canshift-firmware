
#ifndef CANSHIFT_TIMER_CORE_RS_H
#define CANSHIFT_TIMER_CORE_RS_H

#include <stdbool.h>
#include <stdint.h>

#define TIMER_CORE_LAP_CAPACITY 32

#ifdef __cplusplus
extern "C" {
#endif

typedef struct TimerCoreLap {
    uint32_t lap_ms;
    uint32_t total_ms;
    uint16_t index;
    uint16_t session;
} TimerCoreLap;

typedef struct TimerCoreState {
    int64_t last_start_us;
    int64_t accumulated_us;
    int64_t last_lap_total_us;
    uint32_t version;
    uint16_t lap_count;
    uint16_t session;
    uint8_t run_state;
    uint8_t pend_head;
    uint8_t pend_count;
    uint8_t _pad;
    TimerCoreLap pending[TIMER_CORE_LAP_CAPACITY];
} TimerCoreState;

void timer_core_init_rs(TimerCoreState *core);
bool timer_core_start_rs(TimerCoreState *core, int64_t now_us);
bool timer_core_pause_rs(TimerCoreState *core, int64_t now_us);
bool timer_core_resume_rs(TimerCoreState *core, int64_t now_us);
bool timer_core_reset_rs(TimerCoreState *core);
bool timer_core_lap_rs(TimerCoreState *core, int64_t now_us, TimerCoreLap *out_lap,
                       bool *out_dropped_oldest);
uint32_t timer_core_elapsed_ms_rs(const TimerCoreState *core, int64_t now_us);
uint8_t timer_core_pending_count_rs(const TimerCoreState *core);
bool timer_core_pop_pending_rs(TimerCoreState *core, TimerCoreLap *out_lap);

#ifdef __cplusplus
}

static_assert(sizeof(TimerCoreLap) == 12, "TimerCoreLap layout must match rust/timer-core");
static_assert(sizeof(TimerCoreState) == 40 + TIMER_CORE_LAP_CAPACITY * 12,
              "TimerCoreState layout must match rust/timer-core");
#endif

#endif
