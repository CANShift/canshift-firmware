use crate::frame::VehicleBasis;
use crate::vector::{abs, norm, scale, sqrt, Vec3};

pub const LEVEL_SAMPLES: u16 = 100;
pub const LEVEL_MAX_SPREAD_G: f32 = 0.03;
pub const LEVEL_GRAVITY_TOLERANCE_G: f32 = 0.15;

pub const LEVEL_PENDING: u8 = 0;
pub const LEVEL_OK: u8 = 1;
pub const LEVEL_MOVING: u8 = 2;
pub const LEVEL_NOT_ONE_G: u8 = 3;
pub const LEVEL_SCREEN_FLAT: u8 = 4;

#[repr(C)]
#[derive(Clone, Copy, PartialEq, Debug, Default)]
pub struct LevelCapture {
    pub sum: Vec3,
    pub sum_magnitude: f32,
    pub sum_magnitude_sq: f32,
    pub count: u16,
    pub _pad: u16,
}

impl LevelCapture {
    pub fn push(&mut self, screen: Vec3) -> u8 {
        if self.count >= LEVEL_SAMPLES {
            return self.verdict().0;
        }
        let magnitude = norm(screen);
        for (total, value) in self.sum.iter_mut().zip(screen) {
            *total += value;
        }
        self.sum_magnitude += magnitude;
        self.sum_magnitude_sq += magnitude * magnitude;
        self.count += 1;
        self.verdict().0
    }

    pub fn verdict(&self) -> (u8, Vec3) {
        if self.count < LEVEL_SAMPLES {
            return (LEVEL_PENDING, [0.0; 3]);
        }
        let n = f32::from(self.count);
        let mean_magnitude = self.sum_magnitude / n;
        let variance = self.sum_magnitude_sq / n - mean_magnitude * mean_magnitude;
        if sqrt(variance) > LEVEL_MAX_SPREAD_G {
            return (LEVEL_MOVING, [0.0; 3]);
        }
        let gravity = scale(self.sum, 1.0 / n);
        if abs(norm(gravity) - 1.0) > LEVEL_GRAVITY_TOLERANCE_G {
            return (LEVEL_NOT_ONE_G, [0.0; 3]);
        }
        if VehicleBasis::from_gravity(gravity).is_none() {
            return (LEVEL_SCREEN_FLAT, [0.0; 3]);
        }
        (LEVEL_OK, gravity)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    const SHAKE_G: [f32; 2] = [0.2, -0.2];

    fn fill(capture: &mut LevelCapture, sample: impl Fn(u16) -> Vec3) -> u8 {
        let mut status = LEVEL_PENDING;
        for i in 0..LEVEL_SAMPLES {
            status = capture.push(sample(i));
        }
        status
    }

    #[test]
    fn steady_one_g_levels() {
        let mut capture = LevelCapture::default();
        assert_eq!(capture.push([0.0, 1.0, 0.1]), LEVEL_PENDING);
        assert_eq!(fill(&mut capture, |_| [0.0, 1.0, 0.1]), LEVEL_OK);
        let (_, gravity) = capture.verdict();
        assert!(abs(gravity[1] - 1.0) < 1e-3);
    }

    #[test]
    fn a_moving_car_is_refused() {
        let mut capture = LevelCapture::default();
        let shaking = |i: u16| [0.0, 1.0 + SHAKE_G[usize::from(i % 2)], 0.0];
        assert_eq!(fill(&mut capture, shaking), LEVEL_MOVING);
    }

    #[test]
    fn a_reading_far_from_one_g_is_refused() {
        let mut capture = LevelCapture::default();
        assert_eq!(fill(&mut capture, |_| [0.0, 0.5, 0.0]), LEVEL_NOT_ONE_G);
    }

    #[test]
    fn a_flat_screen_is_refused() {
        let mut capture = LevelCapture::default();
        assert_eq!(fill(&mut capture, |_| [0.0, 0.0, 1.0]), LEVEL_SCREEN_FLAT);
    }

    #[test]
    fn samples_past_the_window_are_ignored() {
        let mut capture = LevelCapture::default();
        fill(&mut capture, |_| [0.0, 1.0, 0.0]);
        assert_eq!(capture.push([0.0, 9.0, 0.0]), LEVEL_OK);
        assert_eq!(capture.count, LEVEL_SAMPLES);
    }
}
