use crate::vector::{cross, dot, norm, scale, sub, Vec3};

pub const IMU_AXIS_POS_X: u8 = 0;
pub const IMU_AXIS_NEG_X: u8 = 1;
pub const IMU_AXIS_POS_Y: u8 = 2;
pub const IMU_AXIS_NEG_Y: u8 = 3;
pub const IMU_AXIS_POS_Z: u8 = 4;
pub const IMU_AXIS_NEG_Z: u8 = 5;
const AXIS_CODE_COUNT: u8 = 6;

pub const LEVEL_MIN_HORIZONTAL_NORMAL: f32 = 0.3;

const SCREEN_NORMAL: Vec3 = [0.0, 0.0, 1.0];

#[repr(C)]
#[derive(Clone, Copy, PartialEq, Debug)]
pub struct VehicleBasis {
    pub right: Vec3,
    pub forward: Vec3,
    pub up: Vec3,
}

impl VehicleBasis {
    pub const UPRIGHT_FACING_DRIVER: VehicleBasis = VehicleBasis {
        right: [1.0, 0.0, 0.0],
        forward: [0.0, 0.0, -1.0],
        up: [0.0, 1.0, 0.0],
    };

    pub fn from_gravity(gravity: Vec3) -> Option<VehicleBasis> {
        let magnitude = norm(gravity);
        if magnitude <= 0.0 {
            return None;
        }
        let up = scale(gravity, 1.0 / magnitude);
        let horizontal_normal = sub(SCREEN_NORMAL, scale(up, dot(SCREEN_NORMAL, up)));
        let horizontal = norm(horizontal_normal);
        if horizontal < LEVEL_MIN_HORIZONTAL_NORMAL {
            return None;
        }
        let forward = scale(horizontal_normal, -1.0 / horizontal);
        Some(VehicleBasis {
            right: cross(forward, up),
            forward,
            up,
        })
    }

    pub fn lateral(&self, screen: Vec3) -> f32 {
        dot(screen, self.right)
    }

    pub fn longitudinal(&self, screen: Vec3) -> f32 {
        dot(screen, self.forward)
    }
}

pub fn valid_axis_map(map: [u8; 3]) -> bool {
    let mut seen = [false; 3];
    for code in map {
        if code >= AXIS_CODE_COUNT {
            return false;
        }
        let axis = usize::from(code / 2);
        if seen[axis] {
            return false;
        }
        seen[axis] = true;
    }
    true
}

fn chip_component(chip: Vec3, code: u8) -> f32 {
    let value = chip[usize::from(code / 2) % 3];
    if code.is_multiple_of(2) {
        value
    } else {
        -value
    }
}

pub fn chip_to_screen(chip: Vec3, map: [u8; 3]) -> Vec3 {
    [
        chip_component(chip, map[0]),
        chip_component(chip, map[1]),
        chip_component(chip, map[2]),
    ]
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::vector::abs;

    const IDENTITY: [u8; 3] = [IMU_AXIS_POS_X, IMU_AXIS_POS_Y, IMU_AXIS_POS_Z];

    fn close(a: Vec3, b: Vec3) -> bool {
        (0..3).all(|i| abs(a[i] - b[i]) < 1e-4)
    }

    #[test]
    fn identity_map_passes_axes_through() {
        assert_eq!(chip_to_screen([1.0, 2.0, 3.0], IDENTITY), [1.0, 2.0, 3.0]);
    }

    #[test]
    fn map_swaps_and_negates_axes() {
        let map = [IMU_AXIS_NEG_Y, IMU_AXIS_POS_X, IMU_AXIS_NEG_Z];
        assert_eq!(chip_to_screen([1.0, 2.0, 3.0], map), [-2.0, 1.0, -3.0]);
    }

    #[test]
    fn map_must_name_each_axis_once() {
        assert!(valid_axis_map(IDENTITY));
        assert!(valid_axis_map([
            IMU_AXIS_NEG_Z,
            IMU_AXIS_POS_X,
            IMU_AXIS_NEG_Y
        ]));
        assert!(!valid_axis_map([
            IMU_AXIS_POS_X,
            IMU_AXIS_NEG_X,
            IMU_AXIS_POS_Z
        ]));
        assert!(!valid_axis_map([IMU_AXIS_POS_X, IMU_AXIS_POS_Y, 6]));
    }

    #[test]
    fn upright_screen_levels_to_the_default_basis() {
        let basis = VehicleBasis::from_gravity([0.0, 1.0, 0.0]).unwrap();
        let expected = VehicleBasis::UPRIGHT_FACING_DRIVER;
        assert!(close(basis.right, expected.right));
        assert!(close(basis.forward, expected.forward));
        assert!(close(basis.up, expected.up));
    }

    #[test]
    fn tilted_screen_keeps_forward_horizontal() {
        let tilt = 0.5_f32;
        let basis = VehicleBasis::from_gravity([0.0, 1.0, tilt]).unwrap();
        assert!(abs(dot(basis.forward, basis.up)) < 1e-4);
        assert!(abs(dot(basis.right, basis.up)) < 1e-4);
        assert!(basis.forward[2] < 0.0);
        assert!(close(basis.right, [1.0, 0.0, 0.0]));
    }

    #[test]
    fn screen_lying_flat_cannot_be_levelled() {
        assert!(VehicleBasis::from_gravity([0.0, 0.0, 1.0]).is_none());
        assert!(VehicleBasis::from_gravity([0.0, 0.0, 0.0]).is_none());
    }

    #[test]
    fn forward_acceleration_reads_positive_longitudinal() {
        let basis = VehicleBasis::UPRIGHT_FACING_DRIVER;
        assert!(basis.longitudinal([0.0, 1.0, -0.4]) > 0.39);
        assert!(basis.lateral([0.3, 1.0, 0.0]) > 0.29);
    }
}
