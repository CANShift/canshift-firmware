use super::*;

const START_MS: u32 = 1_000;

fn probe_all(scan: &mut BusScan, frames: [u32; BUS_SCAN_RATE_COUNT], errors: u32) -> u8 {
    let mut now = START_MS;
    let mut action = BUS_SCAN_ACTION_NONE;
    for count in frames {
        (0..count).for_each(|_| on_frame(scan, 0x370));
        now += BUS_SCAN_PROBE_MS;
        action = tick(scan, now, errors);
    }
    action
}

#[test]
fn begin_installs_the_first_rate_listen_only() {
    let mut scan = BusScan::default();
    assert_eq!(begin(&mut scan, START_MS), BUS_SCAN_ACTION_INSTALL);
    assert_eq!(scan.install_kbps, 500);
    assert_eq!(scan.phase, BUS_SCAN_PHASE_PROBING);
}

#[test]
fn a_queued_scan_already_reads_as_the_first_probe() {
    let mut scan = BusScan::default();
    begin(&mut scan, START_MS);
    fail(&mut scan);
    queue(&mut scan);
    assert_eq!(scan.phase, BUS_SCAN_PHASE_PROBING);
    assert_eq!(scan.install_kbps, 500);
    assert_eq!(scan.probe_index, 0);
    assert_eq!(scan.outcome, BUS_SCAN_OUTCOME_NONE);
}

#[test]
fn a_probe_window_holds_until_it_has_elapsed() {
    let mut scan = BusScan::default();
    begin(&mut scan, START_MS);
    assert_eq!(
        tick(&mut scan, START_MS + BUS_SCAN_PROBE_MS - 1, 0),
        BUS_SCAN_ACTION_NONE
    );
    assert_eq!(
        tick(&mut scan, START_MS + BUS_SCAN_PROBE_MS, 0),
        BUS_SCAN_ACTION_INSTALL
    );
    assert_eq!(scan.install_kbps, 250);
}

#[test]
fn every_rate_is_probed_in_order() {
    let mut scan = BusScan::default();
    begin(&mut scan, START_MS);
    let mut installed = [scan.install_kbps; BUS_SCAN_RATE_COUNT];
    for (step, slot) in installed.iter_mut().enumerate().skip(1) {
        tick(&mut scan, START_MS + BUS_SCAN_PROBE_MS * step as u32, 7);
        *slot = scan.install_kbps;
    }
    assert_eq!(installed, BUS_SCAN_RATES_KBPS);
    assert_eq!(scan.probes[0].bus_errors, 7);
}

#[test]
fn a_detected_rate_is_collected_then_restored() {
    let mut scan = BusScan::default();
    begin(&mut scan, START_MS);
    assert_eq!(
        probe_all(&mut scan, [0, 600, 0, 0], 0),
        BUS_SCAN_ACTION_INSTALL
    );
    assert_eq!(scan.phase, BUS_SCAN_PHASE_COLLECTING);
    assert_eq!(scan.install_kbps, 250);
    let collect_from = scan.phase_started_ms;
    [0x370, 0x371, 0x370]
        .into_iter()
        .for_each(|id| on_frame(&mut scan, id));
    assert_eq!(
        tick(&mut scan, collect_from + BUS_SCAN_COLLECT_MS, 0),
        BUS_SCAN_ACTION_RESTORE
    );
    assert_eq!(scan.phase, BUS_SCAN_PHASE_DONE);
    assert_eq!(scan.outcome, BUS_SCAN_OUTCOME_DETECTED);
    assert_eq!(scan.id_count, 2);
    assert_eq!(
        scan.ids[0],
        SeenId {
            id: 0x370,
            count: 2
        }
    );
}

#[test]
fn probe_frames_do_not_reach_the_id_table() {
    let mut scan = BusScan::default();
    begin(&mut scan, START_MS);
    probe_all(&mut scan, [600, 0, 0, 0], 0);
    assert_eq!(scan.id_count, 0);
    assert_eq!(scan.probes[0].frames, 600);
}

#[test]
fn a_silent_bus_restores_without_collecting() {
    let mut scan = BusScan::default();
    begin(&mut scan, START_MS);
    assert_eq!(probe_all(&mut scan, [0; 4], 0), BUS_SCAN_ACTION_RESTORE);
    assert_eq!(scan.outcome, BUS_SCAN_OUTCOME_SILENT);
}

#[test]
fn errors_on_every_rate_are_unclear() {
    let mut scan = BusScan::default();
    begin(&mut scan, START_MS);
    assert_eq!(probe_all(&mut scan, [0; 4], 30), BUS_SCAN_ACTION_RESTORE);
    assert_eq!(scan.outcome, BUS_SCAN_OUTCOME_UNCLEAR);
}

#[test]
fn a_failed_install_ends_the_scan_and_restores() {
    let mut scan = BusScan::default();
    begin(&mut scan, START_MS);
    assert_eq!(fail(&mut scan), BUS_SCAN_ACTION_RESTORE);
    assert_eq!(scan.phase, BUS_SCAN_PHASE_DONE);
    assert_eq!(scan.outcome, BUS_SCAN_OUTCOME_FAILED);
}

#[test]
fn an_abort_replaces_the_previous_result_with_a_failure() {
    let mut scan = BusScan::default();
    begin(&mut scan, START_MS);
    probe_all(&mut scan, [600, 0, 0, 0], 0);
    on_frame(&mut scan, 0x370);
    abort(&mut scan);
    assert_eq!(scan.phase, BUS_SCAN_PHASE_DONE);
    assert_eq!(scan.outcome, BUS_SCAN_OUTCOME_FAILED);
    assert_eq!(scan.id_count, 0);
    assert_eq!(scan.probes[0], Probe::default());
}

#[test]
fn a_finished_scan_ignores_ticks_and_frames() {
    let mut scan = BusScan::default();
    begin(&mut scan, START_MS);
    fail(&mut scan);
    on_frame(&mut scan, 0x10);
    assert_eq!(tick(&mut scan, u32::MAX, 0), BUS_SCAN_ACTION_NONE);
    assert_eq!(scan.id_count, 0);
}

#[test]
fn begin_clears_the_previous_result() {
    let mut scan = BusScan::default();
    begin(&mut scan, START_MS);
    probe_all(&mut scan, [600, 0, 0, 0], 0);
    on_frame(&mut scan, 0x370);
    begin(&mut scan, START_MS);
    assert_eq!(scan.id_count, 0);
    assert_eq!(scan.outcome, BUS_SCAN_OUTCOME_NONE);
    assert_eq!(scan.probes[0].frames, 0);
}

#[test]
fn the_window_survives_millis_wraparound() {
    let mut scan = BusScan::default();
    begin(&mut scan, u32::MAX - 100);
    assert_eq!(tick(&mut scan, 50, 0), BUS_SCAN_ACTION_NONE);
    assert_eq!(
        tick(&mut scan, BUS_SCAN_PROBE_MS, 0),
        BUS_SCAN_ACTION_INSTALL
    );
}
