use super::*;

const SEC: i64 = 1_000_000;

#[test]
fn struct_layout_matches_cxx_header() {
    assert_eq!(core::mem::size_of::<TimerLap>(), 12);
    assert_eq!(core::mem::size_of::<TimerCore>(), 432);
    assert_eq!(core::mem::align_of::<TimerCore>(), 8);
}

#[test]
fn starts_only_from_reset() {
    let mut t = TimerCore::new();
    assert!(t.start(0));
    assert!(!t.start(SEC));
    assert_eq!(t.run_state, RUN_STATE_RUNNING);
    assert_eq!(t.session, 1);
}

#[test]
fn elapsed_accumulates_across_pause_resume() {
    let mut t = TimerCore::new();
    t.start(0);
    assert_eq!(t.elapsed_ms(5 * SEC), 5_000);
    assert!(t.pause(5 * SEC));
    assert_eq!(t.elapsed_ms(60 * SEC), 5_000);
    assert!(t.resume(60 * SEC));
    assert_eq!(t.elapsed_ms(62 * SEC), 7_000);
}

#[test]
fn pause_requires_running_and_resume_requires_paused() {
    let mut t = TimerCore::new();
    assert!(!t.pause(0));
    assert!(!t.resume(0));
    t.start(0);
    assert!(!t.resume(SEC));
    t.pause(SEC);
    assert!(!t.pause(2 * SEC));
}

#[test]
fn reset_clears_run_state_but_keeps_pending_laps() {
    let mut t = TimerCore::new();
    t.start(0);
    t.lap(10 * SEC);
    assert!(t.reset());
    assert_eq!(t.run_state, RUN_STATE_RESET);
    assert_eq!(t.lap_count, 0);
    assert_eq!(t.elapsed_ms(99 * SEC), 0);
    assert_eq!(t.pending_count(), 1);
    assert!(!t.reset());
}

#[test]
fn lap_records_split_and_total() {
    let mut t = TimerCore::new();
    t.start(0);
    let first = t.lap(61 * SEC).unwrap();
    assert_eq!(first.lap.index, 1);
    assert_eq!(first.lap.lap_ms, 61_000);
    assert_eq!(first.lap.total_ms, 61_000);
    assert!(!first.dropped_oldest);

    let second = t.lap(100 * SEC).unwrap();
    assert_eq!(second.lap.index, 2);
    assert_eq!(second.lap.lap_ms, 39_000);
    assert_eq!(second.lap.total_ms, 100_000);
}

#[test]
fn lap_split_spans_pause_correctly() {
    let mut t = TimerCore::new();
    t.start(0);
    t.pause(10 * SEC);
    t.resume(50 * SEC);
    let lap = t.lap(55 * SEC).unwrap();
    assert_eq!(lap.lap.lap_ms, 15_000);
    assert_eq!(lap.lap.total_ms, 15_000);
}

#[test]
fn splits_track_last_and_best_lap() {
    let mut t = TimerCore::new();
    t.start(0);
    assert_eq!(t.last_lap_ms, 0);
    assert_eq!(t.best_lap_ms, 0);
    t.lap(62 * SEC);
    assert_eq!(t.last_lap_ms, 62_000);
    assert_eq!(t.best_lap_ms, 62_000);
    t.lap(120 * SEC);
    assert_eq!(t.last_lap_ms, 58_000);
    assert_eq!(t.best_lap_ms, 58_000);
    t.lap(181 * SEC);
    assert_eq!(t.last_lap_ms, 61_000);
    assert_eq!(t.best_lap_ms, 58_000);
}

#[test]
fn current_lap_runs_from_the_last_split() {
    let mut t = TimerCore::new();
    t.start(0);
    assert_eq!(t.current_lap_ms(30 * SEC), 30_000);
    t.lap(60 * SEC);
    assert_eq!(t.current_lap_ms(75 * SEC), 15_000);
    t.pause(80 * SEC);
    assert_eq!(t.current_lap_ms(500 * SEC), 20_000);
}

#[test]
fn reset_and_new_session_clear_the_splits() {
    let mut t = TimerCore::new();
    t.start(0);
    t.lap(60 * SEC);
    t.reset();
    assert_eq!(t.last_lap_ms, 0);
    assert_eq!(t.best_lap_ms, 0);
    assert_eq!(t.current_lap_ms(99 * SEC), 0);
    t.start(100 * SEC);
    assert_eq!(t.best_lap_ms, 0);
    assert_eq!(t.current_lap_ms(110 * SEC), 10_000);
}

#[test]
fn lap_ignored_unless_running() {
    let mut t = TimerCore::new();
    assert!(t.lap(0).is_none());
    t.start(0);
    t.pause(SEC);
    assert!(t.lap(2 * SEC).is_none());
}

#[test]
fn pending_ring_flushes_in_order() {
    let mut t = TimerCore::new();
    t.start(0);
    for i in 1..=3 {
        t.lap(i * SEC);
    }
    let popped: Vec<u16> = core::iter::from_fn(|| t.pop_pending())
        .map(|lap| lap.index)
        .collect();
    assert_eq!(popped, vec![1, 2, 3]);
    assert_eq!(t.pending_count(), 0);
    assert!(t.pop_pending().is_none());
}

#[test]
fn pending_ring_drops_oldest_on_overflow() {
    let mut t = TimerCore::new();
    t.start(0);
    let mut dropped_seen = false;
    for i in 1..=(LAP_CAPACITY as i64 + 2) {
        let outcome = t.lap(i * SEC).unwrap();
        dropped_seen |= outcome.dropped_oldest;
    }
    assert!(dropped_seen);
    assert_eq!(t.pending_count() as usize, LAP_CAPACITY);
    let first = t.pop_pending().unwrap();
    assert_eq!(first.index, 3);
    assert_eq!(t.lap_count as usize, LAP_CAPACITY + 2);
}

#[test]
fn session_increments_on_each_start() {
    let mut t = TimerCore::new();
    t.start(0);
    t.lap(SEC);
    t.reset();
    t.start(10 * SEC);
    assert_eq!(t.session, 2);
    let lap = t.lap(11 * SEC).unwrap();
    assert_eq!(lap.lap.session, 2);
    assert_eq!(lap.lap.index, 1);
}

#[test]
fn version_bumps_on_every_mutation() {
    let mut t = TimerCore::new();
    let mut last = t.version;
    t.start(0);
    assert_ne!(t.version, last);
    last = t.version;
    t.lap(SEC);
    assert_ne!(t.version, last);
    last = t.version;
    t.pause(2 * SEC);
    assert_ne!(t.version, last);
    last = t.version;
    t.resume(3 * SEC);
    assert_ne!(t.version, last);
    last = t.version;
    t.reset();
    assert_ne!(t.version, last);
}

#[test]
fn rejected_transitions_do_not_bump_version() {
    let mut t = TimerCore::new();
    let before = t.version;
    t.pause(0);
    t.resume(0);
    t.reset();
    t.lap(0);
    assert_eq!(t.version, before);
}

#[test]
fn elapsed_is_robust_against_clock_anomalies() {
    let mut t = TimerCore::new();
    t.start(100 * SEC);
    assert_eq!(t.elapsed_ms(99 * SEC), 0);
}
