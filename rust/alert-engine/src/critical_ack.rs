pub const CRITICAL_NO_SOURCE: i8 = -1;
pub const CRITICAL_SOURCE_CAPACITY: u8 = 32;

#[must_use]
pub fn acknowledge(acknowledged: u32, source: i8) -> u32 {
    if source < 0 || source as u8 >= CRITICAL_SOURCE_CAPACITY {
        return acknowledged;
    }
    acknowledged | (1u32 << source)
}

#[must_use]
pub fn retain(acknowledged: u32, critical: u32) -> u32 {
    acknowledged & critical
}

#[must_use]
pub fn hold(shown: u32, critical: u32, acknowledged: u32, no_data: u32) -> u32 {
    critical | (shown & !acknowledged & no_data)
}

#[must_use]
pub fn first_unacknowledged(critical: u32, acknowledged: u32) -> i8 {
    let open = critical & !acknowledged;
    if open == 0 {
        return CRITICAL_NO_SOURCE;
    }
    open.trailing_zeros() as i8
}

#[cfg(test)]
mod tests {
    use super::*;

    const OIL_PRESSURE: i8 = 0;
    const COOLANT: i8 = 1;
    const OIL_TEMP: i8 = 2;

    fn bit(source: i8) -> u32 {
        1u32 << source
    }

    struct Takeover {
        acknowledged: u32,
    }

    impl Takeover {
        fn show(&mut self, critical: u32) -> i8 {
            self.acknowledged = retain(self.acknowledged, critical);
            first_unacknowledged(critical, self.acknowledged)
        }

        fn ack(&mut self, source: i8) {
            self.acknowledged = acknowledge(self.acknowledged, source);
        }
    }

    #[test]
    fn shows_the_first_critical_source() {
        let mut takeover = Takeover { acknowledged: 0 };
        assert_eq!(takeover.show(bit(COOLANT) | bit(OIL_TEMP)), COOLANT);
        assert_eq!(takeover.show(0), CRITICAL_NO_SOURCE);
    }

    #[test]
    fn an_acknowledged_source_does_not_mask_a_later_one() {
        let mut takeover = Takeover { acknowledged: 0 };
        assert_eq!(takeover.show(bit(OIL_PRESSURE)), OIL_PRESSURE);
        takeover.ack(OIL_PRESSURE);
        assert_eq!(takeover.show(bit(OIL_PRESSURE)), CRITICAL_NO_SOURCE);
        assert_eq!(takeover.show(bit(OIL_PRESSURE) | bit(COOLANT)), COOLANT);
    }

    #[test]
    fn a_recovered_source_shows_again_when_it_re_trips() {
        let mut takeover = Takeover { acknowledged: 0 };
        let shown = takeover.show(bit(OIL_PRESSURE));
        takeover.ack(shown);
        assert_eq!(takeover.show(bit(COOLANT)), COOLANT);
        assert_eq!(
            takeover.show(bit(OIL_PRESSURE) | bit(COOLANT)),
            OIL_PRESSURE
        );
    }

    #[test]
    fn every_acknowledged_source_stays_hidden_while_critical() {
        let mut takeover = Takeover { acknowledged: 0 };
        let both = bit(OIL_PRESSURE) | bit(COOLANT);
        let first = takeover.show(both);
        takeover.ack(first);
        let second = takeover.show(both);
        takeover.ack(second);
        assert_eq!(second, COOLANT);
        assert_eq!(takeover.show(both), CRITICAL_NO_SOURCE);
    }

    #[test]
    fn a_shown_source_stays_until_acknowledged_while_its_signal_is_lost() {
        let shown = hold(0, bit(OIL_PRESSURE), 0, 0);
        let lost = hold(shown, 0, 0, bit(OIL_PRESSURE));
        assert_eq!(lost, bit(OIL_PRESSURE));
        assert_eq!(hold(lost, 0, bit(OIL_PRESSURE), bit(OIL_PRESSURE)), 0);
    }

    #[test]
    fn a_source_that_recovers_with_live_data_clears_itself() {
        let shown = hold(0, bit(COOLANT), 0, 0);
        assert_eq!(hold(shown, 0, 0, 0), 0);
    }

    #[test]
    fn a_lost_signal_never_raises_a_takeover_on_its_own() {
        assert_eq!(hold(0, 0, 0, bit(OIL_TEMP)), 0);
    }

    #[test]
    fn out_of_range_sources_are_ignored() {
        assert_eq!(acknowledge(0, CRITICAL_NO_SOURCE), 0);
        assert_eq!(acknowledge(0, CRITICAL_SOURCE_CAPACITY as i8), 0);
    }
}
