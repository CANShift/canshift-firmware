#![cfg_attr(not(feature = "std"), no_std)]

#[cfg(feature = "ffi")]
mod ffi;

mod core;
mod envelope;
mod frame;
mod launch;
mod level;
mod vector;

pub use crate::core::{ImuCore, LOW_PASS_ALPHA};
pub use envelope::{envelope_vertex, felt_force, plot_offset, sector_of, PlotOffset, SECTOR_COUNT};
pub use frame::{
    chip_to_screen, valid_axis_map, VehicleBasis, IMU_AXIS_NEG_X, IMU_AXIS_NEG_Y, IMU_AXIS_NEG_Z,
    IMU_AXIS_POS_X, IMU_AXIS_POS_Y, IMU_AXIS_POS_Z, LEVEL_MIN_HORIZONTAL_NORMAL,
};
pub use launch::{
    targets_for, LaunchSample, LaunchTargets, LaunchTimer, LAUNCH_ABORT_G, LAUNCH_ARMED,
    LAUNCH_ARM_MS, LAUNCH_DONE, LAUNCH_IDLE, LAUNCH_IMPERIAL, LAUNCH_METRIC, LAUNCH_PUSH_G,
    LAUNCH_PUSH_HOLD_MS, LAUNCH_REST_G, LAUNCH_REST_KPH, LAUNCH_RUNNING, LAUNCH_TIMEOUT_MS,
};
pub use level::{
    LevelCapture, LEVEL_GRAVITY_TOLERANCE_G, LEVEL_MAX_SPREAD_G, LEVEL_MOVING, LEVEL_NOT_ONE_G,
    LEVEL_OK, LEVEL_PENDING, LEVEL_SAMPLES, LEVEL_SCREEN_FLAT,
};
