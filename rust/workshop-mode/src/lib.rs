#![cfg_attr(not(feature = "std"), no_std)]

#[cfg(feature = "ffi")]
mod ffi;

mod credentials;
mod join_payload;
mod motion;
mod tuner_assets;

pub use credentials::{
    credentials_valid, generate_credentials, Credentials, WORKSHOP_ENTROPY_BYTES,
    WORKSHOP_PASSWORD_CAP, WORKSHOP_PASSWORD_LEN, WORKSHOP_SSID_CAP,
};
pub use join_payload::{join_payload, WORKSHOP_JOIN_PAYLOAD_CAP};
pub use motion::{
    motion_exit_due, moving, MotionWatch, WORKSHOP_MOTION_EXIT_KPH, WORKSHOP_MOTION_EXIT_MS,
};
pub use tuner_assets::{
    content_type, spa_route, tuner_path, DEFAULT_CONTENT_TYPE, WORKSHOP_TUNER_PATH_CAP,
};

pub const WORKSHOP_STATE_DRIVING: u8 = 0;
pub const WORKSHOP_STATE_ENTERING: u8 = 1;
pub const WORKSHOP_STATE_ACTIVE: u8 = 2;

pub const WORKSHOP_EVENT_REQUEST: u8 = 0;
pub const WORKSHOP_EVENT_ENTERED: u8 = 1;
pub const WORKSHOP_EVENT_FAILED: u8 = 2;
pub const WORKSHOP_EVENT_RESUME: u8 = 3;

pub const WORKSHOP_RESUME_NONE: u8 = 0;
pub const WORKSHOP_RESUME_UPDATE: u8 = 1;
pub const WORKSHOP_RESUME_CONFIG: u8 = 2;

pub const WORKSHOP_MIN_FILESYSTEM_BYTES: u32 = 4 * 1024 * 1024;

const STATIONARY_ROUNDING_KPH: f32 = 0.5;

#[repr(u8)]
#[derive(Clone, Copy, PartialEq, Eq, Debug, Default)]
pub enum WorkshopState {
    #[default]
    Driving = WORKSHOP_STATE_DRIVING,
    Entering = WORKSHOP_STATE_ENTERING,
    Active = WORKSHOP_STATE_ACTIVE,
}

impl WorkshopState {
    #[must_use]
    pub fn from_u8(raw: u8) -> Self {
        match raw {
            WORKSHOP_STATE_ENTERING => Self::Entering,
            WORKSHOP_STATE_ACTIVE => Self::Active,
            _ => Self::Driving,
        }
    }
}

#[repr(u8)]
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum WorkshopEvent {
    Request = WORKSHOP_EVENT_REQUEST,
    Entered = WORKSHOP_EVENT_ENTERED,
    Failed = WORKSHOP_EVENT_FAILED,
    Resume = WORKSHOP_EVENT_RESUME,
}

impl WorkshopEvent {
    #[must_use]
    pub fn from_u8(raw: u8) -> Option<Self> {
        match raw {
            WORKSHOP_EVENT_REQUEST => Some(Self::Request),
            WORKSHOP_EVENT_ENTERED => Some(Self::Entered),
            WORKSHOP_EVENT_FAILED => Some(Self::Failed),
            WORKSHOP_EVENT_RESUME => Some(Self::Resume),
            _ => None,
        }
    }
}

#[must_use]
pub fn next_state(state: WorkshopState, event: WorkshopEvent, offered: bool) -> WorkshopState {
    match (state, event) {
        (WorkshopState::Driving, WorkshopEvent::Request) if offered => WorkshopState::Entering,
        (WorkshopState::Driving, WorkshopEvent::Resume) => WorkshopState::Entering,
        (WorkshopState::Entering, WorkshopEvent::Entered) => WorkshopState::Active,
        (WorkshopState::Entering, WorkshopEvent::Failed) => WorkshopState::Driving,
        _ => state,
    }
}

#[must_use]
pub fn board_supported(filesystem_bytes: u32) -> bool {
    filesystem_bytes >= WORKSHOP_MIN_FILESYSTEM_BYTES
}

#[must_use]
pub fn stationary(speed_mapped: bool, speed_valid: bool, speed_kph: f32) -> bool {
    if !speed_mapped || !speed_valid {
        return true;
    }
    speed_kph.abs() < STATIONARY_ROUNDING_KPH
}

#[must_use]
pub fn offered(
    filesystem_bytes: u32,
    speed_mapped: bool,
    speed_valid: bool,
    speed_kph: f32,
) -> bool {
    board_supported(filesystem_bytes) && stationary(speed_mapped, speed_valid, speed_kph)
}

#[repr(u8)]
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum ResumeReason {
    Update = WORKSHOP_RESUME_UPDATE,
    Config = WORKSHOP_RESUME_CONFIG,
}

impl ResumeReason {
    #[must_use]
    pub fn from_u8(raw: u8) -> Option<Self> {
        match raw {
            WORKSHOP_RESUME_UPDATE => Some(Self::Update),
            WORKSHOP_RESUME_CONFIG => Some(Self::Config),
            _ => None,
        }
    }
}

#[must_use]
pub fn resume_after_reboot(
    reason: Option<ResumeReason>,
    filesystem_bytes: u32,
    image_pending_verify: bool,
) -> bool {
    let reason_holds = match reason {
        Some(ResumeReason::Update) => image_pending_verify,
        Some(ResumeReason::Config) => true,
        None => false,
    };
    reason_holds && board_supported(filesystem_bytes)
}

#[cfg(test)]
mod tests {
    use super::*;

    const SIXTEEN_MB_LAYOUT_FILESYSTEM: u32 = 0x76_0000;
    const FOUR_MB_LAYOUT_FILESYSTEM: u32 = 0xD_0000;

    #[test]
    fn a_request_enters_only_when_offered() {
        assert_eq!(
            next_state(WorkshopState::Driving, WorkshopEvent::Request, true),
            WorkshopState::Entering
        );
        assert_eq!(
            next_state(WorkshopState::Driving, WorkshopEvent::Request, false),
            WorkshopState::Driving
        );
    }

    #[test]
    fn entering_settles_on_success_and_falls_back_on_failure() {
        assert_eq!(
            next_state(WorkshopState::Entering, WorkshopEvent::Entered, true),
            WorkshopState::Active
        );
        assert_eq!(
            next_state(WorkshopState::Entering, WorkshopEvent::Failed, true),
            WorkshopState::Driving
        );
    }

    #[test]
    fn active_is_left_only_by_a_reboot() {
        for event in [
            WorkshopEvent::Request,
            WorkshopEvent::Entered,
            WorkshopEvent::Failed,
        ] {
            assert_eq!(
                next_state(WorkshopState::Active, event, true),
                WorkshopState::Active
            );
        }
    }

    #[test]
    fn a_second_request_while_entering_is_ignored() {
        assert_eq!(
            next_state(WorkshopState::Entering, WorkshopEvent::Request, true),
            WorkshopState::Entering
        );
    }

    #[test]
    fn stray_events_leave_driving_alone() {
        assert_eq!(
            next_state(WorkshopState::Driving, WorkshopEvent::Entered, true),
            WorkshopState::Driving
        );
        assert_eq!(
            next_state(WorkshopState::Driving, WorkshopEvent::Failed, true),
            WorkshopState::Driving
        );
    }

    #[test]
    fn a_resume_enters_from_driving_without_the_stationary_gate() {
        assert_eq!(
            next_state(WorkshopState::Driving, WorkshopEvent::Resume, false),
            WorkshopState::Entering
        );
    }

    #[test]
    fn a_resume_never_reenters_an_entry_already_under_way() {
        for state in [WorkshopState::Entering, WorkshopState::Active] {
            assert_eq!(next_state(state, WorkshopEvent::Resume, true), state);
        }
    }

    #[test]
    fn an_update_resumes_only_on_its_first_boot_on_a_supported_board() {
        let update = Some(ResumeReason::Update);
        assert!(resume_after_reboot(
            update,
            SIXTEEN_MB_LAYOUT_FILESYSTEM,
            true
        ));
        assert!(!resume_after_reboot(
            None,
            SIXTEEN_MB_LAYOUT_FILESYSTEM,
            true
        ));
        assert!(!resume_after_reboot(
            update,
            SIXTEEN_MB_LAYOUT_FILESYSTEM,
            false
        ));
        assert!(!resume_after_reboot(
            update,
            FOUR_MB_LAYOUT_FILESYSTEM,
            true
        ));
    }

    #[test]
    fn a_config_restart_resumes_without_a_pending_image() {
        let config = Some(ResumeReason::Config);
        assert!(resume_after_reboot(
            config,
            SIXTEEN_MB_LAYOUT_FILESYSTEM,
            false
        ));
        assert!(resume_after_reboot(
            config,
            SIXTEEN_MB_LAYOUT_FILESYSTEM,
            true
        ));
        assert!(!resume_after_reboot(
            config,
            FOUR_MB_LAYOUT_FILESYSTEM,
            false
        ));
    }

    #[test]
    fn resume_reasons_decode_safely() {
        assert_eq!(ResumeReason::from_u8(WORKSHOP_RESUME_NONE), None);
        assert_eq!(
            ResumeReason::from_u8(WORKSHOP_RESUME_UPDATE),
            Some(ResumeReason::Update)
        );
        assert_eq!(
            ResumeReason::from_u8(WORKSHOP_RESUME_CONFIG),
            Some(ResumeReason::Config)
        );
        assert_eq!(ResumeReason::from_u8(9), None);
    }

    #[test]
    fn unknown_raw_values_decode_safely() {
        assert_eq!(WorkshopState::from_u8(9), WorkshopState::Driving);
        assert_eq!(WorkshopEvent::from_u8(9), None);
    }

    #[test]
    fn only_the_sixteen_megabyte_layout_carries_workshop_mode() {
        assert!(board_supported(SIXTEEN_MB_LAYOUT_FILESYSTEM));
        assert!(!board_supported(FOUR_MB_LAYOUT_FILESYSTEM));
        assert!(!board_supported(0));
    }

    #[test]
    fn no_mapped_speed_counts_as_stationary() {
        assert!(stationary(false, false, 0.0));
        assert!(stationary(false, true, 120.0));
    }

    #[test]
    fn a_stale_speed_counts_as_stationary() {
        assert!(stationary(true, false, 80.0));
    }

    #[test]
    fn a_live_speed_must_read_zero() {
        assert!(stationary(true, true, 0.0));
        assert!(stationary(true, true, 0.4));
        assert!(stationary(true, true, -0.4));
        assert!(!stationary(true, true, 0.5));
        assert!(!stationary(true, true, 42.0));
    }

    #[test]
    fn a_nan_speed_is_not_stationary() {
        assert!(!stationary(true, true, f32::NAN));
    }

    #[test]
    fn offered_needs_both_the_board_and_a_stopped_car() {
        assert!(offered(SIXTEEN_MB_LAYOUT_FILESYSTEM, true, true, 0.0));
        assert!(!offered(SIXTEEN_MB_LAYOUT_FILESYSTEM, true, true, 30.0));
        assert!(!offered(FOUR_MB_LAYOUT_FILESYSTEM, false, false, 0.0));
    }
}
