use super::*;

const BOOST: usize = 0;
const FUEL: usize = 1;
const RETARD: usize = 3;
const REV: usize = 4;
const TRACTION: usize = 5;
const LIMP: usize = 8;

fn bit(kind: usize) -> u16 {
    1u16 << kind
}

fn collect(state: &CutBandState, now_ms: u32) -> (u8, [CutRow; CUT_ROW_CAPACITY]) {
    let mut out = [CutRow::default(); CUT_ROW_CAPACITY];
    let count = rows(state, now_ms, &mut out);
    (count, out)
}

#[test]
fn raises_on_the_same_step_as_the_flag() {
    let mut state = CutBandState::default();
    step(&mut state, bit(BOOST), 1_000);
    let (count, out) = collect(&state, 1_000);
    assert_eq!(count, 1);
    assert_eq!(out[0].kind, BOOST as u8);
    assert_eq!(out[0].elapsed_ms, 0);
}

#[test]
fn holds_a_sixty_millisecond_cut_for_the_minimum() {
    let mut state = CutBandState::default();
    step(&mut state, bit(BOOST), 0);
    step(&mut state, 0, 60);
    assert_eq!(collect(&state, 60).0, 1);
    step(&mut state, 0, CUT_MIN_VISIBLE_MS - 1);
    assert_eq!(collect(&state, CUT_MIN_VISIBLE_MS - 1).0, 1);
    step(&mut state, 0, CUT_MIN_VISIBLE_MS);
    assert_eq!(collect(&state, CUT_MIN_VISIBLE_MS).0, 0);
}

#[test]
fn elapsed_freezes_when_the_cut_ends() {
    let mut state = CutBandState::default();
    step(&mut state, bit(FUEL), 0);
    step(&mut state, bit(FUEL), 800);
    assert_eq!(collect(&state, 800).1[0].elapsed_ms, 800);
    step(&mut state, 0, 900);
    assert_eq!(collect(&state, 1_400).1[0].elapsed_ms, 900);
}

#[test]
fn a_cut_that_never_ends_keeps_counting() {
    let mut state = CutBandState::default();
    step(&mut state, bit(REV), 0);
    step(&mut state, bit(REV), 9_000);
    assert_eq!(collect(&state, 9_000).1[0].elapsed_ms, 9_000);
}

#[test]
fn most_severe_sits_on_top() {
    let mut state = CutBandState::default();
    step(&mut state, bit(BOOST), 0);
    step(&mut state, bit(BOOST) | bit(LIMP), 100);
    let (count, out) = collect(&state, 100);
    assert_eq!(count, 2);
    assert_eq!(out[0].kind, LIMP as u8);
    assert_eq!(out[1].kind, BOOST as u8);
}

#[test]
fn equal_severity_orders_oldest_first() {
    let mut state = CutBandState::default();
    step(&mut state, bit(RETARD), 0);
    step(&mut state, bit(RETARD) | bit(BOOST), 100);
    let out = collect(&state, 100).1;
    assert_eq!(out[0].kind, RETARD as u8);
    assert_eq!(out[1].kind, BOOST as u8);
}

#[test]
fn stacks_three_at_most_keeping_the_worst() {
    let mut state = CutBandState::default();
    step(
        &mut state,
        bit(BOOST) | bit(RETARD) | bit(TRACTION) | bit(REV) | bit(LIMP),
        0,
    );
    let (count, out) = collect(&state, 0);
    assert_eq!(count, CUT_ROW_CAPACITY as u8);
    assert_eq!(out[0].kind, LIMP as u8);
    assert_eq!(out[1].kind, BOOST as u8);
    assert_eq!(out[2].kind, RETARD as u8);
}

#[test]
fn a_re_raised_cut_restarts_its_clock() {
    let mut state = CutBandState::default();
    step(&mut state, bit(BOOST), 0);
    step(&mut state, 0, 100);
    step(&mut state, bit(BOOST), 200);
    assert_eq!(collect(&state, 400).1[0].elapsed_ms, 200);
}

#[test]
fn severity_and_readout_come_from_the_kind_table() {
    assert_eq!(kind_severity(FUEL as u8), Severity::Failure);
    assert_eq!(kind_severity(BOOST as u8), Severity::Warning);
    assert_eq!(CUT_KINDS[LIMP].readout, CutReadout::Latched);
    assert_eq!(CUT_KINDS[RETARD].readout, CutReadout::Holding);
}

#[test]
fn unknown_kind_is_nameless() {
    assert_eq!(kind_name(CUT_KIND_COUNT as u8), b"\0");
    assert_eq!(kind_severity(CUT_KIND_COUNT as u8), Severity::Information);
}

#[test]
fn every_name_is_nul_terminated() {
    for spec in &CUT_KINDS {
        assert_eq!(*spec.name.last().unwrap(), 0);
    }
}
