use crate::vector::{abs, hypot};

pub const SECTOR_COUNT: usize = 8;

const TAN_22_5: f32 = 0.414_213_57;
const DIAGONAL: f32 = 0.707_106_77;

const SECTOR_DIRECTIONS: [(f32, f32); SECTOR_COUNT] = [
    (0.0, 1.0),
    (DIAGONAL, DIAGONAL),
    (1.0, 0.0),
    (DIAGONAL, -DIAGONAL),
    (0.0, -1.0),
    (-DIAGONAL, -DIAGONAL),
    (-1.0, 0.0),
    (-DIAGONAL, DIAGONAL),
];

#[derive(Clone, Copy, PartialEq, Debug)]
pub struct PlotOffset {
    pub dx: i16,
    pub dy: i16,
}

pub fn felt_force(lateral: f32, longitudinal: f32) -> (f32, f32) {
    (-lateral, -longitudinal)
}

fn diagonal_sector(x: f32, y: f32) -> usize {
    match (x >= 0.0, y >= 0.0) {
        (true, true) => 1,
        (true, false) => 3,
        (false, false) => 5,
        (false, true) => 7,
    }
}

pub fn sector_of(x: f32, y: f32) -> usize {
    if abs(x) <= TAN_22_5 * abs(y) {
        return if y >= 0.0 {
            0
        } else {
            4
        };
    }
    if abs(y) <= TAN_22_5 * abs(x) {
        return if x >= 0.0 {
            2
        } else {
            6
        };
    }
    diagonal_sector(x, y)
}

pub fn record_peak(peaks: &mut [f32; SECTOR_COUNT], x: f32, y: f32) {
    let magnitude = hypot(x, y);
    let slot = &mut peaks[sector_of(x, y)];
    if magnitude > *slot {
        *slot = magnitude;
    }
}

pub fn plot_offset(x: f32, y: f32, range_g: f32, radius_px: i16) -> PlotOffset {
    if range_g <= 0.0 || radius_px <= 0 {
        return PlotOffset { dx: 0, dy: 0 };
    }
    let magnitude = hypot(x, y);
    let clamp = if magnitude > range_g {
        range_g / magnitude
    } else {
        1.0
    };
    let k = clamp * f32::from(radius_px) / range_g;
    PlotOffset {
        dx: (x * k) as i16,
        dy: -(y * k) as i16,
    }
}

pub fn envelope_vertex(
    peaks: &[f32; SECTOR_COUNT],
    sector: usize,
    range_g: f32,
    radius_px: i16,
) -> PlotOffset {
    let (ux, uy) = SECTOR_DIRECTIONS[sector % SECTOR_COUNT];
    let peak = peaks[sector % SECTOR_COUNT];
    plot_offset(ux * peak, uy * peak, range_g, radius_px)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn braking_plots_up_and_a_left_turn_plots_right() {
        assert_eq!(felt_force(0.0, -0.8), (-0.0, 0.8));
        assert_eq!(felt_force(-0.5, 0.0), (0.5, -0.0));
    }

    #[test]
    fn sectors_run_clockwise_from_up() {
        assert_eq!(sector_of(0.0, 1.0), 0);
        assert_eq!(sector_of(0.7, 0.7), 1);
        assert_eq!(sector_of(1.0, 0.1), 2);
        assert_eq!(sector_of(0.7, -0.7), 3);
        assert_eq!(sector_of(0.1, -1.0), 4);
        assert_eq!(sector_of(-0.7, -0.7), 5);
        assert_eq!(sector_of(-1.0, 0.0), 6);
        assert_eq!(sector_of(-0.7, 0.7), 7);
    }

    #[test]
    fn peaks_only_grow() {
        let mut peaks = [0.0; SECTOR_COUNT];
        record_peak(&mut peaks, 0.0, 0.9);
        record_peak(&mut peaks, 0.0, 0.4);
        assert!(abs(peaks[0] - 0.9) < 1e-4);
        assert_eq!(peaks[4], 0.0);
    }

    #[test]
    fn plot_maps_range_to_radius_with_screen_y_down() {
        assert_eq!(
            plot_offset(0.0, 1.5, 1.5, 40),
            PlotOffset { dx: 0, dy: -40 }
        );
        assert_eq!(
            plot_offset(0.75, 0.0, 1.5, 40),
            PlotOffset { dx: 20, dy: 0 }
        );
    }

    #[test]
    fn plot_pins_an_overrange_point_to_the_ring() {
        assert_eq!(plot_offset(3.0, 0.0, 1.5, 40), PlotOffset { dx: 40, dy: 0 });
    }

    #[test]
    fn plot_with_no_range_stays_at_the_centre() {
        assert_eq!(plot_offset(1.0, 1.0, 0.0, 40), PlotOffset { dx: 0, dy: 0 });
        assert_eq!(plot_offset(1.0, 1.0, 1.5, 0), PlotOffset { dx: 0, dy: 0 });
    }

    #[test]
    fn envelope_vertex_points_along_its_sector() {
        let mut peaks = [0.0; SECTOR_COUNT];
        peaks[2] = 0.75;
        assert_eq!(
            envelope_vertex(&peaks, 2, 1.5, 40),
            PlotOffset { dx: 20, dy: 0 }
        );
        assert_eq!(
            envelope_vertex(&peaks, 0, 1.5, 40),
            PlotOffset { dx: 0, dy: 0 }
        );
    }
}
