
#ifndef CANSHIFT_CONTROL_STATE_RS_H
#define CANSHIFT_CONTROL_STATE_RS_H

#include <stdbool.h>
#include <stdint.h>

#define CONTROL_STATE_COUNT 4
#define CONTROL_STEP_MAX 6
#define CONTROL_LONG_PRESS_MS 600
#define CONTROL_SPLASH_CHANGE_MS 800
#define CONTROL_SPLASH_REFUSAL_MS 1200
#define CONTROL_SPLASH_KIND_COUNT 2

#ifdef __cplusplus
extern "C" {
#endif

enum {
    CONTROL_STATE_OFF = 0,
    CONTROL_STATE_ARMED = 1,
    CONTROL_STATE_ACTIVE = 2,
    CONTROL_STATE_UNAVAILABLE = 3
};

enum { CONTROL_SPLASH_KIND_CHANGE = 0, CONTROL_SPLASH_KIND_REFUSAL = 1 };

typedef struct {
    uint32_t press_start_ms;
    uint8_t level;
    bool pressed;
    bool long_fired;
    uint8_t _pad;
} ControlStepperRs;

typedef struct {
    uint32_t started_ms;
    uint32_t hold_ms;
    bool showing;
    uint8_t _pad[3];
} ControlSplashRs;

uint8_t control_state_resolve_rs(bool blocked, bool acting, bool requested);
uint8_t control_step_tap_rs(uint8_t level);
void control_stepper_init_rs(ControlStepperRs *stepper, uint8_t level);
void control_stepper_press_rs(ControlStepperRs *stepper, uint32_t now_ms);
bool control_stepper_poll_rs(ControlStepperRs *stepper, uint32_t now_ms);
bool control_stepper_release_rs(ControlStepperRs *stepper, uint32_t now_ms);
bool control_stepper_sync_rs(ControlStepperRs *stepper, uint8_t level);
uint32_t control_splash_hold_ms_rs(uint8_t kind);
void control_splash_raise_rs(ControlSplashRs *timer, uint8_t kind, uint32_t now_ms);
bool control_splash_poll_rs(ControlSplashRs *timer, uint32_t now_ms);
bool control_splash_preempt_rs(ControlSplashRs *timer);

#ifdef __cplusplus
}

static_assert(sizeof(ControlStepperRs) == 8,
              "ControlStepperRs layout must match rust/control-state");
static_assert(sizeof(ControlSplashRs) == 12,
              "ControlSplashRs layout must match rust/control-state");
#endif

#endif
