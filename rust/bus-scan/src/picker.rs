use crate::scan::Probe;

pub const BUS_SCAN_MIN_FRAMES: u32 = 4;
pub const BUS_SCAN_ERROR_RATIO: u32 = 10;

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Pick {
    Detected(usize),
    Silent,
    Unclear,
}

fn clean(probe: &Probe) -> bool {
    probe.frames >= BUS_SCAN_MIN_FRAMES && probe.bus_errors <= probe.frames / BUS_SCAN_ERROR_RATIO
}

fn quiet(probe: &Probe) -> bool {
    probe.frames == 0 && probe.bus_errors == 0
}

#[must_use]
pub fn pick(probes: &[Probe]) -> Pick {
    let best = probes
        .iter()
        .enumerate()
        .filter(|(_, probe)| clean(probe))
        .fold(
            None,
            |best: Option<(usize, u32)>, (index, probe)| match best {
                Some((_, frames)) if frames >= probe.frames => best,
                _ => Some((index, probe.frames)),
            },
        );
    if let Some((index, _)) = best {
        return Pick::Detected(index);
    }
    if probes.iter().all(quiet) {
        return Pick::Silent;
    }
    Pick::Unclear
}

#[cfg(test)]
mod tests {
    use super::*;

    fn probe(frames: u32, bus_errors: u32) -> Probe {
        Probe {
            frames,
            bus_errors,
            ..Probe::default()
        }
    }

    #[test]
    fn the_rate_with_clean_frames_wins() {
        let probes = [probe(0, 90), probe(800, 0), probe(0, 40), probe(0, 70)];
        assert_eq!(pick(&probes), Pick::Detected(1));
    }

    #[test]
    fn the_busiest_clean_rate_wins_over_a_sparse_one() {
        let probes = [probe(5, 0), probe(400, 3), probe(0, 0), probe(0, 0)];
        assert_eq!(pick(&probes), Pick::Detected(1));
    }

    #[test]
    fn a_tie_keeps_the_earlier_probe() {
        let probes = [probe(300, 0), probe(300, 0), probe(0, 0), probe(0, 0)];
        assert_eq!(pick(&probes), Pick::Detected(0));
    }

    #[test]
    fn frames_drowned_in_errors_are_not_clean() {
        let probes = [probe(20, 3), probe(0, 50), probe(0, 0), probe(0, 0)];
        assert_eq!(pick(&probes), Pick::Unclear);
    }

    #[test]
    fn errors_up_to_a_tenth_of_the_frames_are_tolerated() {
        let probes = [probe(100, 10), probe(0, 0), probe(0, 0), probe(0, 0)];
        assert_eq!(pick(&probes), Pick::Detected(0));
    }

    #[test]
    fn a_handful_of_frames_is_not_enough() {
        let probes = [probe(3, 0), probe(0, 0), probe(0, 0), probe(0, 0)];
        assert_eq!(pick(&probes), Pick::Unclear);
    }

    #[test]
    fn nothing_on_any_rate_is_a_silent_bus() {
        let probes = [Probe::default(); 4];
        assert_eq!(pick(&probes), Pick::Silent);
    }

    #[test]
    fn errors_alone_are_unclear_not_silent() {
        let probes = [probe(0, 12), probe(0, 0), probe(0, 0), probe(0, 0)];
        assert_eq!(pick(&probes), Pick::Unclear);
    }

    #[test]
    fn saturated_error_counts_do_not_overflow() {
        let probes = [probe(u32::MAX, u32::MAX), probe(0, 0)];
        assert_eq!(pick(&probes), Pick::Unclear);
    }
}
