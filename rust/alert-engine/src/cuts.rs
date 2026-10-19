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
mod tests;
