use crate::picker::{pick, Pick};
use crate::seen_ids::{record_id, SeenId, BUS_SCAN_ID_CAPACITY};

pub const BUS_SCAN_RATE_COUNT: usize = 4;
pub const BUS_SCAN_RATES_KBPS: [u16; BUS_SCAN_RATE_COUNT] = [500, 250, 1000, 125];
pub const BUS_SCAN_PROBE_MS: u32 = 500;
pub const BUS_SCAN_COLLECT_MS: u32 = 3000;

pub const BUS_SCAN_PHASE_IDLE: u8 = 0;
pub const BUS_SCAN_PHASE_PROBING: u8 = 1;
pub const BUS_SCAN_PHASE_COLLECTING: u8 = 2;
pub const BUS_SCAN_PHASE_DONE: u8 = 3;

pub const BUS_SCAN_OUTCOME_NONE: u8 = 0;
pub const BUS_SCAN_OUTCOME_DETECTED: u8 = 1;
pub const BUS_SCAN_OUTCOME_SILENT: u8 = 2;
pub const BUS_SCAN_OUTCOME_UNCLEAR: u8 = 3;
pub const BUS_SCAN_OUTCOME_FAILED: u8 = 4;

pub const BUS_SCAN_ACTION_NONE: u8 = 0;
pub const BUS_SCAN_ACTION_INSTALL: u8 = 1;
pub const BUS_SCAN_ACTION_RESTORE: u8 = 2;

#[repr(C)]
#[derive(Clone, Copy, PartialEq, Eq, Debug, Default)]
pub struct Probe {
    pub frames: u32,
    pub bus_errors: u32,
    pub kbps: u16,
    pub reserved: u16,
}

#[repr(C)]
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub struct BusScan {
    pub probes: [Probe; BUS_SCAN_RATE_COUNT],
    pub ids: [SeenId; BUS_SCAN_ID_CAPACITY],
    pub phase_started_ms: u32,
    pub id_count: u16,
    pub install_kbps: u16,
    pub phase: u8,
    pub probe_index: u8,
    pub outcome: u8,
    pub truncated: bool,
}

impl Default for BusScan {
    fn default() -> Self {
        Self {
            probes: [Probe::default(); BUS_SCAN_RATE_COUNT],
            ids: [SeenId::default(); BUS_SCAN_ID_CAPACITY],
            phase_started_ms: 0,
            id_count: 0,
            install_kbps: 0,
            phase: BUS_SCAN_PHASE_IDLE,
            probe_index: 0,
            outcome: BUS_SCAN_OUTCOME_NONE,
            truncated: false,
        }
    }
}

fn enter(scan: &mut BusScan, phase: u8, kbps: u16, now_ms: u32) -> u8 {
    scan.phase = phase;
    scan.install_kbps = kbps;
    scan.phase_started_ms = now_ms;
    BUS_SCAN_ACTION_INSTALL
}

fn finish(scan: &mut BusScan, outcome: u8) -> u8 {
    scan.phase = BUS_SCAN_PHASE_DONE;
    scan.outcome = outcome;
    BUS_SCAN_ACTION_RESTORE
}

pub fn queue(scan: &mut BusScan) {
    *scan = BusScan::default();
    for (probe, kbps) in scan.probes.iter_mut().zip(BUS_SCAN_RATES_KBPS) {
        probe.kbps = kbps;
    }
    scan.phase = BUS_SCAN_PHASE_PROBING;
    scan.install_kbps = BUS_SCAN_RATES_KBPS[0];
}

pub fn begin(scan: &mut BusScan, now_ms: u32) -> u8 {
    queue(scan);
    enter(scan, BUS_SCAN_PHASE_PROBING, BUS_SCAN_RATES_KBPS[0], now_ms)
}

pub fn on_frame(scan: &mut BusScan, id: u32) {
    if scan.phase == BUS_SCAN_PHASE_COLLECTING {
        record_id(&mut scan.ids, &mut scan.id_count, &mut scan.truncated, id);
        return;
    }
    if scan.phase != BUS_SCAN_PHASE_PROBING {
        return;
    }
    if let Some(probe) = scan.probes.get_mut(usize::from(scan.probe_index)) {
        probe.frames = probe.frames.saturating_add(1);
    }
}

fn window_ms(phase: u8) -> Option<u32> {
    match phase {
        BUS_SCAN_PHASE_PROBING => Some(BUS_SCAN_PROBE_MS),
        BUS_SCAN_PHASE_COLLECTING => Some(BUS_SCAN_COLLECT_MS),
        _ => None,
    }
}

fn after_probes(scan: &mut BusScan, now_ms: u32) -> u8 {
    match pick(&scan.probes) {
        Pick::Detected(index) => {
            scan.outcome = BUS_SCAN_OUTCOME_DETECTED;
            enter(
                scan,
                BUS_SCAN_PHASE_COLLECTING,
                scan.probes[index].kbps,
                now_ms,
            )
        }
        Pick::Silent => finish(scan, BUS_SCAN_OUTCOME_SILENT),
        Pick::Unclear => finish(scan, BUS_SCAN_OUTCOME_UNCLEAR),
    }
}

fn after_probe(scan: &mut BusScan, now_ms: u32, bus_errors: u32) -> u8 {
    let index = usize::from(scan.probe_index);
    if let Some(probe) = scan.probes.get_mut(index) {
        probe.bus_errors = bus_errors;
    }
    let Some(&next) = BUS_SCAN_RATES_KBPS.get(index + 1) else {
        return after_probes(scan, now_ms);
    };
    scan.probe_index += 1;
    enter(scan, BUS_SCAN_PHASE_PROBING, next, now_ms)
}

pub fn tick(scan: &mut BusScan, now_ms: u32, bus_errors: u32) -> u8 {
    let Some(window) = window_ms(scan.phase) else {
        return BUS_SCAN_ACTION_NONE;
    };
    if now_ms.wrapping_sub(scan.phase_started_ms) < window {
        return BUS_SCAN_ACTION_NONE;
    }
    if scan.phase == BUS_SCAN_PHASE_PROBING {
        return after_probe(scan, now_ms, bus_errors);
    }
    finish(scan, BUS_SCAN_OUTCOME_DETECTED)
}

pub fn fail(scan: &mut BusScan) -> u8 {
    finish(scan, BUS_SCAN_OUTCOME_FAILED)
}

pub fn abort(scan: &mut BusScan) {
    *scan = BusScan::default();
    scan.phase = BUS_SCAN_PHASE_DONE;
    scan.outcome = BUS_SCAN_OUTCOME_FAILED;
}

#[cfg(test)]
mod tests;
