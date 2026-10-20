use super::*;

const METRIC: LaunchTargets = LAUNCH_METRIC;
const STEP_MS: u32 = 10;
const ACCEL_G: f32 = 0.5;
const ACCEL_KPH_PER_S: f32 = ACCEL_G * 9.80665 * 3.6;

fn sample(now_ms: u32, longitudinal_g: f32, speed_kph: f32) -> LaunchSample {
    LaunchSample {
        now_ms,
        longitudinal_g,
        speed_kph: Some(speed_kph),
    }
}

fn rest(timer: &mut LaunchTimer, from_ms: u32, until_ms: u32) -> u32 {
    let mut now = from_ms;
    while now <= until_ms {
        timer.tick(sample(now, 0.0, 0.0), METRIC);
        now += STEP_MS;
    }
    now
}

fn accelerate(timer: &mut LaunchTimer, from_ms: u32, for_ms: u32) -> u32 {
    let mut now = from_ms;
    while now < from_ms + for_ms && timer.state != LAUNCH_DONE {
        let speed = (now - from_ms) as f32 / 1000.0 * ACCEL_KPH_PER_S;
        timer.tick(sample(now, ACCEL_G, speed), METRIC);
        now += STEP_MS;
    }
    now
}

#[test]
fn struct_layout_matches_cxx_header() {
    assert_eq!(core::mem::size_of::<LaunchTimer>(), 32);
    assert_eq!(core::mem::size_of::<LaunchTargets>(), 8);
}

#[test]
fn arms_after_a_second_at_rest() {
    let mut timer = LaunchTimer::default();
    rest(&mut timer, 0, LAUNCH_ARM_MS - STEP_MS);
    assert_eq!(timer.state, LAUNCH_IDLE);
    rest(&mut timer, LAUNCH_ARM_MS, LAUNCH_ARM_MS);
    assert_eq!(timer.state, LAUNCH_ARMED);
}

#[test]
fn never_arms_without_a_speed_signal() {
    let mut timer = LaunchTimer::default();
    for now in (0..3000).step_by(10) {
        let blind = LaunchSample {
            now_ms: now,
            longitudinal_g: 0.0,
            speed_kph: None,
        };
        timer.tick(blind, METRIC);
    }
    assert_eq!(timer.state, LAUNCH_IDLE);
}

#[test]
fn a_brief_shove_does_not_start_the_run() {
    let mut timer = LaunchTimer::default();
    let now = rest(&mut timer, 0, LAUNCH_ARM_MS);
    timer.tick(sample(now, 0.4, 0.0), METRIC);
    timer.tick(sample(now + 50, 0.0, 0.0), METRIC);
    timer.tick(sample(now + 160, 0.4, 0.0), METRIC);
    assert_eq!(timer.state, LAUNCH_ARMED);
}

#[test]
fn a_launch_is_back_dated_to_the_push_onset() {
    let mut timer = LaunchTimer::default();
    let now = rest(&mut timer, 0, LAUNCH_ARM_MS);
    accelerate(&mut timer, now, LAUNCH_PUSH_HOLD_MS + STEP_MS);
    assert_eq!(timer.state, LAUNCH_RUNNING);
    assert_eq!(timer.start_ms, now);
}

#[test]
fn times_the_sprint_and_the_strip() {
    let mut timer = LaunchTimer::default();
    let now = rest(&mut timer, 0, LAUNCH_ARM_MS);
    accelerate(&mut timer, now, 20_000);
    assert_eq!(timer.state, LAUNCH_DONE);
    let sprint_s = 100.0 / ACCEL_KPH_PER_S;
    assert!((timer.sprint_ms as f32 / 1000.0 - sprint_s).abs() < 0.05);
    let strip_s = (2.0 * 400.0 / (ACCEL_G * 9.80665)).sqrt();
    assert!((timer.strip_ms as f32 / 1000.0 - strip_s).abs() < 0.1);
}

#[test]
fn hard_braking_ends_the_run_and_keeps_a_finished_sprint() {
    let mut timer = LaunchTimer::default();
    let now = rest(&mut timer, 0, LAUNCH_ARM_MS);
    let after = accelerate(&mut timer, now, 6_500);
    assert!(timer.sprint_ms != 0);
    timer.tick(sample(after, -0.5, 110.0), METRIC);
    assert_eq!(timer.state, LAUNCH_DONE);
    assert_eq!(timer.strip_ms, 0);
}

#[test]
fn braking_before_any_target_discards_the_run() {
    let mut timer = LaunchTimer::default();
    let now = rest(&mut timer, 0, LAUNCH_ARM_MS);
    let after = accelerate(&mut timer, now, 1_000);
    timer.tick(sample(after, -0.5, 20.0), METRIC);
    assert_eq!(timer.state, LAUNCH_IDLE);
}

#[test]
fn rolling_away_without_a_push_disarms() {
    let mut timer = LaunchTimer::default();
    let now = rest(&mut timer, 0, LAUNCH_ARM_MS);
    timer.tick(sample(now, 0.1, 5.0), METRIC);
    assert_eq!(timer.state, LAUNCH_IDLE);
}

#[test]
fn a_run_times_out() {
    let mut timer = LaunchTimer::default();
    let now = rest(&mut timer, 0, LAUNCH_ARM_MS);
    accelerate(&mut timer, now, 200);
    timer.tick(sample(now + LAUNCH_TIMEOUT_MS + 1, 0.0, 40.0), METRIC);
    assert_eq!(timer.state, LAUNCH_IDLE);
}

#[test]
fn results_survive_re_arming_until_the_next_launch() {
    let mut timer = LaunchTimer::default();
    let now = rest(&mut timer, 0, LAUNCH_ARM_MS);
    let after = accelerate(&mut timer, now, 20_000);
    let sprint = timer.sprint_ms;
    let later = rest(&mut timer, after, after + LAUNCH_ARM_MS);
    assert_eq!(timer.state, LAUNCH_ARMED);
    assert_eq!(timer.sprint_ms, sprint);
    accelerate(&mut timer, later, LAUNCH_PUSH_HOLD_MS + STEP_MS);
    assert_eq!(timer.sprint_ms, 0);
}

#[test]
fn elapsed_reads_zero_outside_a_run() {
    let timer = LaunchTimer::default();
    assert_eq!(timer.elapsed_ms(5_000), 0);
}

#[test]
fn imperial_targets_are_sixty_mph_and_a_quarter_mile() {
    assert_eq!(targets_for(false), LAUNCH_METRIC);
    let imperial = targets_for(true);
    assert!((imperial.sprint_kph / 1.609_344 - 60.0).abs() < 1e-3);
    assert!((imperial.strip_m / 1609.344 - 0.25).abs() < 1e-5);
}
