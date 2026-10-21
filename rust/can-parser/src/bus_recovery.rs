pub const CAN_BUS_STATE_STOPPED: u8 = 0;
pub const CAN_BUS_STATE_RUNNING: u8 = 1;
pub const CAN_BUS_STATE_BUS_OFF: u8 = 2;
pub const CAN_BUS_STATE_RECOVERING: u8 = 3;

pub const CAN_BUS_ACTION_NONE: u8 = 0;
pub const CAN_BUS_ACTION_RECOVER: u8 = 1;
pub const CAN_BUS_ACTION_RESTART: u8 = 2;
pub const CAN_BUS_ACTION_GIVE_UP: u8 = 3;

pub const MAX_BUS_OFF_RECOVERIES: u8 = 10;

#[repr(u8)]
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum BusAction {
    None = CAN_BUS_ACTION_NONE,
    Recover = CAN_BUS_ACTION_RECOVER,
    Restart = CAN_BUS_ACTION_RESTART,
    GiveUp = CAN_BUS_ACTION_GIVE_UP,
}

#[must_use]
pub fn bus_action(driver_state: u8, recoveries: u8) -> BusAction {
    match driver_state {
        CAN_BUS_STATE_BUS_OFF if recoveries < MAX_BUS_OFF_RECOVERIES => BusAction::Recover,
        CAN_BUS_STATE_BUS_OFF if recoveries == MAX_BUS_OFF_RECOVERIES => BusAction::GiveUp,
        CAN_BUS_STATE_STOPPED => BusAction::Restart,
        _ => BusAction::None,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn a_bus_off_walks_recover_then_restart() {
        let sequence = [
            CAN_BUS_STATE_RUNNING,
            CAN_BUS_STATE_BUS_OFF,
            CAN_BUS_STATE_RECOVERING,
            CAN_BUS_STATE_STOPPED,
            CAN_BUS_STATE_RUNNING,
        ];
        let actions: [BusAction; 5] = sequence.map(|state| bus_action(state, 0));
        assert_eq!(
            actions,
            [
                BusAction::None,
                BusAction::Recover,
                BusAction::None,
                BusAction::Restart,
                BusAction::None,
            ]
        );
    }

    #[test]
    fn an_unknown_state_is_left_alone() {
        assert_eq!(bus_action(9, 0), BusAction::None);
    }

    #[test]
    fn recovery_gives_up_once_after_the_cap() {
        let last_allowed = MAX_BUS_OFF_RECOVERIES - 1;
        assert_eq!(
            bus_action(CAN_BUS_STATE_BUS_OFF, last_allowed),
            BusAction::Recover
        );
        assert_eq!(
            bus_action(CAN_BUS_STATE_BUS_OFF, MAX_BUS_OFF_RECOVERIES),
            BusAction::GiveUp
        );
        assert_eq!(
            bus_action(CAN_BUS_STATE_BUS_OFF, MAX_BUS_OFF_RECOVERIES + 1),
            BusAction::None
        );
    }
}
