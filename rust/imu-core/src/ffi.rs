#![allow(clippy::missing_safety_doc)]

use crate::envelope::{envelope_vertex, felt_force, plot_offset, PlotOffset};
use crate::{
    targets_for, ImuCore, LaunchSample, LaunchTargets, LaunchTimer, LevelCapture, LEVEL_PENDING,
};

unsafe fn write_offset(offset: PlotOffset, out_dx: *mut i16, out_dy: *mut i16) {
    if let Some(dx) = unsafe { out_dx.as_mut() } {
        *dx = offset.dx;
    }
    if let Some(dy) = unsafe { out_dy.as_mut() } {
        *dy = offset.dy;
    }
}

#[no_mangle]
pub unsafe extern "C" fn imu_core_init_rs(core: *mut ImuCore, axis_map: *const u8) -> bool {
    let (Some(core), false) = (unsafe { core.as_mut() }, axis_map.is_null()) else {
        return false;
    };
    let map = unsafe { [*axis_map, *axis_map.add(1), *axis_map.add(2)] };
    let Some(fresh) = ImuCore::new(map) else {
        return false;
    };
    *core = fresh;
    true
}

#[no_mangle]
pub unsafe extern "C" fn imu_core_push_rs(core: *mut ImuCore, ax: f32, ay: f32, az: f32) {
    if let Some(core) = unsafe { core.as_mut() } {
        core.push([ax, ay, az]);
    }
}

#[no_mangle]
pub unsafe extern "C" fn imu_core_level_rs(core: *mut ImuCore, gx: f32, gy: f32, gz: f32) -> bool {
    let Some(core) = (unsafe { core.as_mut() }) else {
        return false;
    };
    core.level([gx, gy, gz])
}

#[no_mangle]
pub unsafe extern "C" fn imu_core_reset_peaks_rs(core: *mut ImuCore) {
    if let Some(core) = unsafe { core.as_mut() } {
        core.reset_peaks();
    }
}

#[no_mangle]
pub unsafe extern "C" fn imu_core_combined_g_rs(core: *const ImuCore) -> f32 {
    unsafe { core.as_ref() }.map_or(0.0, ImuCore::combined_g)
}

#[no_mangle]
pub unsafe extern "C" fn imu_core_plot_rs(
    core: *const ImuCore,
    range_g: f32,
    radius_px: i16,
    out_dx: *mut i16,
    out_dy: *mut i16,
) {
    let Some(core) = (unsafe { core.as_ref() }) else {
        return;
    };
    let (x, y) = felt_force(core.lateral_g, core.longitudinal_g);
    unsafe { write_offset(plot_offset(x, y, range_g, radius_px), out_dx, out_dy) };
}

#[no_mangle]
pub unsafe extern "C" fn imu_core_envelope_vertex_rs(
    core: *const ImuCore,
    sector: u8,
    range_g: f32,
    radius_px: i16,
    out_dx: *mut i16,
    out_dy: *mut i16,
) {
    let Some(core) = (unsafe { core.as_ref() }) else {
        return;
    };
    let vertex = envelope_vertex(&core.peaks, usize::from(sector), range_g, radius_px);
    unsafe { write_offset(vertex, out_dx, out_dy) };
}

#[no_mangle]
pub unsafe extern "C" fn imu_level_push_rs(
    capture: *mut LevelCapture,
    core: *const ImuCore,
    ax: f32,
    ay: f32,
    az: f32,
) -> u8 {
    let (Some(capture), Some(core)) = (unsafe { capture.as_mut() }, unsafe { core.as_ref() })
    else {
        return LEVEL_PENDING;
    };
    capture.push(core.screen([ax, ay, az]))
}

#[no_mangle]
pub unsafe extern "C" fn imu_level_gravity_rs(capture: *const LevelCapture, out: *mut f32) -> u8 {
    let (Some(capture), false) = (unsafe { capture.as_ref() }, out.is_null()) else {
        return LEVEL_PENDING;
    };
    let (status, gravity) = capture.verdict();
    for (i, value) in gravity.iter().enumerate() {
        unsafe { *out.add(i) = *value };
    }
    status
}

#[no_mangle]
pub unsafe extern "C" fn imu_launch_tick_rs(
    timer: *mut LaunchTimer,
    now_ms: u32,
    longitudinal_g: f32,
    has_speed: bool,
    speed_kph: f32,
    targets: LaunchTargets,
) {
    let Some(timer) = (unsafe { timer.as_mut() }) else {
        return;
    };
    let sample = LaunchSample {
        now_ms,
        longitudinal_g,
        speed_kph: has_speed.then_some(speed_kph),
    };
    timer.tick(sample, targets);
}

#[no_mangle]
pub unsafe extern "C" fn imu_launch_elapsed_ms_rs(timer: *const LaunchTimer, now_ms: u32) -> u32 {
    unsafe { timer.as_ref() }.map_or(0, |timer| timer.elapsed_ms(now_ms))
}

#[no_mangle]
pub extern "C" fn imu_launch_targets_rs(imperial: bool) -> LaunchTargets {
    targets_for(imperial)
}
