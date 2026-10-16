pub const WORKSHOP_SOCKET_BUSY_WINDOW_MS: u32 = 5000;

pub const WORKSHOP_ADMIT_ADOPT: u8 = 0;
pub const WORKSHOP_ADMIT_REPLACE_STALE: u8 = 1;
pub const WORKSHOP_ADMIT_REFUSE_BUSY: u8 = 2;

#[repr(u8)]
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Admission {
    Adopt = WORKSHOP_ADMIT_ADOPT,
    ReplaceStale = WORKSHOP_ADMIT_REPLACE_STALE,
    RefuseBusy = WORKSHOP_ADMIT_REFUSE_BUSY,
}

#[must_use]
pub fn socket_admission(
    has_client: bool,
    same_client: bool,
    transfer_in_flight: bool,
    last_activity_ms: u32,
    now_ms: u32,
) -> Admission {
    if !has_client || same_client {
        return Admission::Adopt;
    }
    if transfer_in_flight {
        return Admission::RefuseBusy;
    }
    if now_ms.wrapping_sub(last_activity_ms) < WORKSHOP_SOCKET_BUSY_WINDOW_MS {
        return Admission::RefuseBusy;
    }
    Admission::ReplaceStale
}

#[cfg(test)]
mod tests {
    use super::*;

    const SEEN_MS: u32 = 10_000;

    #[test]
    fn a_first_tuner_is_adopted() {
        assert_eq!(
            socket_admission(false, false, false, SEEN_MS, SEEN_MS),
            Admission::Adopt
        );
    }

    #[test]
    fn an_active_tuner_keeps_the_socket() {
        let inside = SEEN_MS + WORKSHOP_SOCKET_BUSY_WINDOW_MS - 1;
        assert_eq!(
            socket_admission(true, false, false, SEEN_MS, inside),
            Admission::RefuseBusy
        );
    }

    #[test]
    fn a_stale_tuner_is_replaced() {
        let edge = SEEN_MS + WORKSHOP_SOCKET_BUSY_WINDOW_MS;
        assert_eq!(
            socket_admission(true, false, false, SEEN_MS, edge),
            Admission::ReplaceStale
        );
    }

    #[test]
    fn the_same_socket_is_readopted() {
        assert_eq!(
            socket_admission(true, true, false, SEEN_MS, SEEN_MS + 1),
            Admission::Adopt
        );
    }

    #[test]
    fn the_window_survives_a_millis_wrap() {
        assert_eq!(
            socket_admission(true, false, false, u32::MAX - 100, 100),
            Admission::RefuseBusy
        );
    }

    #[test]
    fn a_silent_tuner_mid_transfer_keeps_the_socket() {
        let late = SEEN_MS + WORKSHOP_SOCKET_BUSY_WINDOW_MS * 4;
        assert_eq!(
            socket_admission(true, false, true, SEEN_MS, late),
            Admission::RefuseBusy
        );
    }

    #[test]
    fn the_transfer_owner_is_readopted() {
        let late = SEEN_MS + WORKSHOP_SOCKET_BUSY_WINDOW_MS * 4;
        assert_eq!(
            socket_admission(true, true, true, SEEN_MS, late),
            Admission::Adopt
        );
    }
}
