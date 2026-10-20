use crate::envelope::{felt_force, record_peak, SECTOR_COUNT};
use crate::frame::{chip_to_screen, valid_axis_map, VehicleBasis};
use crate::vector::{hypot, Vec3};

pub const LOW_PASS_ALPHA: f32 = 0.24;

#[repr(C)]
#[derive(Clone, Copy, PartialEq, Debug)]
pub struct ImuCore {
    pub basis: VehicleBasis,
    pub peaks: [f32; SECTOR_COUNT],
    pub lateral_g: f32,
    pub longitudinal_g: f32,
    pub axis_map: [u8; 3],
    pub primed: u8,
}

impl ImuCore {
    pub fn new(axis_map: [u8; 3]) -> Option<ImuCore> {
        if !valid_axis_map(axis_map) {
            return None;
        }
        Some(ImuCore {
            basis: VehicleBasis::UPRIGHT_FACING_DRIVER,
            peaks: [0.0; SECTOR_COUNT],
            lateral_g: 0.0,
            longitudinal_g: 0.0,
            axis_map,
            primed: 0,
        })
    }

    pub fn screen(&self, chip: Vec3) -> Vec3 {
        chip_to_screen(chip, self.axis_map)
    }

    pub fn level(&mut self, gravity: Vec3) -> bool {
        let Some(basis) = VehicleBasis::from_gravity(gravity) else {
            return false;
        };
        self.basis = basis;
        self.reset_peaks();
        true
    }

    pub fn push(&mut self, chip: Vec3) {
        let screen = self.screen(chip);
        let lateral = self.basis.lateral(screen);
        let longitudinal = self.basis.longitudinal(screen);
        self.filter(lateral, longitudinal);
        let (x, y) = felt_force(self.lateral_g, self.longitudinal_g);
        record_peak(&mut self.peaks, x, y);
    }

    fn filter(&mut self, lateral: f32, longitudinal: f32) {
        if self.primed == 0 {
            self.lateral_g = lateral;
            self.longitudinal_g = longitudinal;
            self.primed = 1;
            return;
        }
        self.lateral_g += LOW_PASS_ALPHA * (lateral - self.lateral_g);
        self.longitudinal_g += LOW_PASS_ALPHA * (longitudinal - self.longitudinal_g);
    }

    pub fn reset_peaks(&mut self) {
        self.peaks = [0.0; SECTOR_COUNT];
    }

    pub fn combined_g(&self) -> f32 {
        hypot(self.lateral_g, self.longitudinal_g)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::frame::{IMU_AXIS_POS_X, IMU_AXIS_POS_Y, IMU_AXIS_POS_Z};
    use crate::vector::abs;

    const IDENTITY: [u8; 3] = [IMU_AXIS_POS_X, IMU_AXIS_POS_Y, IMU_AXIS_POS_Z];

    fn settle(core: &mut ImuCore, chip: Vec3) {
        for _ in 0..200 {
            core.push(chip);
        }
    }

    #[test]
    fn struct_layout_matches_cxx_header() {
        assert_eq!(core::mem::size_of::<VehicleBasis>(), 36);
        assert_eq!(core::mem::size_of::<ImuCore>(), 80);
    }

    #[test]
    fn a_bad_axis_map_is_refused() {
        assert!(ImuCore::new([IMU_AXIS_POS_X, IMU_AXIS_POS_X, IMU_AXIS_POS_Z]).is_none());
    }

    #[test]
    fn at_rest_upright_reads_zero_g() {
        let mut core = ImuCore::new(IDENTITY).unwrap();
        settle(&mut core, [0.0, 1.0, 0.0]);
        assert!(core.combined_g() < 1e-4);
    }

    #[test]
    fn braking_reads_negative_longitudinal_and_peaks_up() {
        let mut core = ImuCore::new(IDENTITY).unwrap();
        settle(&mut core, [0.0, 1.0, 0.8]);
        assert!(abs(core.longitudinal_g + 0.8) < 1e-3);
        assert!(abs(core.peaks[0] - 0.8) < 1e-3);
    }

    #[test]
    fn the_filter_primes_on_the_first_sample_then_lags() {
        let mut core = ImuCore::new(IDENTITY).unwrap();
        core.push([0.5, 1.0, 0.0]);
        assert!(abs(core.lateral_g - 0.5) < 1e-4);
        core.push([1.5, 1.0, 0.0]);
        assert!(abs(core.lateral_g - (0.5 + LOW_PASS_ALPHA)) < 1e-4);
    }

    #[test]
    fn levelling_rebuilds_the_basis_and_clears_peaks() {
        let mut core = ImuCore::new(IDENTITY).unwrap();
        settle(&mut core, [0.0, 1.0, 0.8]);
        assert!(core.level([0.0, 0.9, 0.3]));
        assert_eq!(core.peaks, [0.0; SECTOR_COUNT]);
        assert!(!core.level([0.0, 0.0, 1.0]));
    }
}
