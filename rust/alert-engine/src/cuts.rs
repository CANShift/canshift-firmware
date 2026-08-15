use crate::Severity;

pub const CUT_KIND_COUNT: usize = 9;
pub const CUT_ROW_CAPACITY: usize = 3;
pub const CUT_MIN_VISIBLE_MS: u32 = 1500;

#[repr(u8)]
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum CutReadout {
    Elapsed = 0,
    Holding = 1,
    Latched = 2,
}

pub struct CutKindSpec {
    pub name: &'static [u8],
    pub severity: Severity,
    pub readout: CutReadout,
}

pub const CUT_KINDS: [CutKindSpec; CUT_KIND_COUNT] = [
    CutKindSpec {
        name: b"BOOST CUT\0",
        severity: Severity::Warning,
        readout: CutReadout::Elapsed,
    },
    CutKindSpec {
        name: b"FUEL CUT\0",
        severity: Severity::Failure,
        readout: CutReadout::Elapsed,
    },
    CutKindSpec {
        name: b"IGNITION CUT\0",
        severity: Severity::Warning,
        readout: CutReadout::Elapsed,
    },
    CutKindSpec {
        name: b"IGNITION RETARD\0",
        severity: Severity::Warning,
        readout: CutReadout::Holding,
    },
    CutKindSpec {
        name: b"REV LIMIT\0",
        severity: Severity::Warning,
        readout: CutReadout::Elapsed,
    },
    CutKindSpec {
        name: b"TRACTION CUT\0",
        severity: Severity::Warning,
        readout: CutReadout::Elapsed,
    },
    CutKindSpec {
        name: b"PIT LIMIT CUT\0",
        severity: Severity::Warning,
        readout: CutReadout::Holding,
    },
    CutKindSpec {
        name: b"OVERHEAT PROTECT\0",
        severity: Severity::Failure,
        readout: CutReadout::Holding,
    },
    CutKindSpec {
        name: b"LIMP MODE\0",
        severity: Severity::Failure,
        readout: CutReadout::Latched,
    },
];

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct CutBandState {
    pub raised_at_ms: [u32; CUT_KIND_COUNT],
    pub ended_at_ms: [u32; CUT_KIND_COUNT],
    pub active: [bool; CUT_KIND_COUNT],
    pub visible: [bool; CUT_KIND_COUNT],
}

impl Default for CutBandState {
    fn default() -> Self {
        Self {
            raised_at_ms: [0; CUT_KIND_COUNT],
            ended_at_ms: [0; CUT_KIND_COUNT],
            active: [false; CUT_KIND_COUNT],
            visible: [false; CUT_KIND_COUNT],
        }
    }
}

#[repr(C)]
#[derive(Clone, Copy, Default, PartialEq, Eq, Debug)]
pub struct CutRow {
    pub kind: u8,
    pub severity: u8,
    pub readout: u8,
    pub elapsed_ms: u32,
}

#[must_use]
fn held(state: &CutBandState, kind: usize, now_ms: u32) -> bool {
    now_ms.wrapping_sub(state.raised_at_ms[kind]) < CUT_MIN_VISIBLE_MS
}

fn step_one(state: &mut CutBandState, kind: usize, asserted: bool, now_ms: u32) {
    if asserted && !state.active[kind] {
        state.raised_at_ms[kind] = now_ms;
    }
    if !asserted && state.active[kind] {
        state.ended_at_ms[kind] = now_ms;
    }
    state.active[kind] = asserted;
    state.visible[kind] = asserted || (state.visible[kind] && held(state, kind, now_ms));
}

pub fn step(state: &mut CutBandState, flags: u16, now_ms: u32) {
    for kind in 0..CUT_KIND_COUNT {
        let asserted = flags & (1u16 << kind) != 0;
        step_one(state, kind, asserted, now_ms);
    }
}

#[must_use]
fn elapsed_ms(state: &CutBandState, kind: usize, now_ms: u32) -> u32 {
    let end = if state.active[kind] {
        now_ms
    } else {
        state.ended_at_ms[kind]
    };
    end.wrapping_sub(state.raised_at_ms[kind])
}

#[must_use]
fn outranks(state: &CutBandState, candidate: usize, incumbent: usize) -> bool {
    let lhs = CUT_KINDS[candidate].severity as u8;
    let rhs = CUT_KINDS[incumbent].severity as u8;
    if lhs != rhs {
        return lhs > rhs;
    }
    state.raised_at_ms[candidate] < state.raised_at_ms[incumbent]
}

fn insert(rows: &mut [CutRow; CUT_ROW_CAPACITY], count: usize, at: usize, row: CutRow) {
    let mut i = count.min(CUT_ROW_CAPACITY - 1);
    while i > at {
        rows[i] = rows[i - 1];
        i -= 1;
    }
    rows[at] = row;
}

#[must_use]
fn rank_of(
    state: &CutBandState,
    rows: &[CutRow; CUT_ROW_CAPACITY],
    count: usize,
    kind: usize,
) -> usize {
    for (slot, row) in rows.iter().enumerate().take(count) {
        if outranks(state, kind, row.kind as usize) {
            return slot;
        }
    }
    count
}

#[must_use]
pub fn rows(state: &CutBandState, now_ms: u32, out: &mut [CutRow; CUT_ROW_CAPACITY]) -> u8 {
    let mut count = 0usize;
    for (kind, spec) in CUT_KINDS.iter().enumerate() {
        if !state.visible[kind] {
            continue;
        }
        let at = rank_of(state, out, count, kind);
        if at >= CUT_ROW_CAPACITY {
            continue;
        }
        insert(
            out,
            count,
            at,
            CutRow {
                kind: kind as u8,
                severity: spec.severity as u8,
                readout: spec.readout as u8,
                elapsed_ms: elapsed_ms(state, kind, now_ms),
            },
        );
        count = (count + 1).min(CUT_ROW_CAPACITY);
    }
    count as u8
}

#[must_use]
pub fn kind_name(kind: u8) -> &'static [u8] {
    let idx = kind as usize;
    if idx >= CUT_KIND_COUNT {
        return b"\0";
    }
    CUT_KINDS[idx].name
}

#[must_use]
pub fn kind_severity(kind: u8) -> Severity {
    let idx = kind as usize;
    if idx >= CUT_KIND_COUNT {
        return Severity::Information;
    }
    CUT_KINDS[idx].severity
}

#[cfg(test)]
mod tests {
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
}
