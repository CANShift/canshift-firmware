#![cfg_attr(not(feature = "std"), no_std)]

#[cfg(feature = "ffi")]
mod ffi;

#[cfg(all(feature = "ffi", not(any(test, feature = "std"))))]
#[panic_handler]
fn panic(_info: &core::panic::PanicInfo) -> ! {
    loop {
        core::hint::spin_loop();
    }
}

pub const COLUMNS: u8 = 12;
pub const ROWS: u8 = 12;
pub const GUTTER: u32 = 12;
pub const FRAME_PADDING: u32 = 16;

#[repr(C)]
#[derive(Clone, Copy, PartialEq, Eq, Debug, Default)]
pub struct GridRect {
    pub x: i16,
    pub y: i16,
    pub w: i16,
    pub h: i16,
}

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub struct GridPlacement {
    pub col: u8,
    pub col_span: u8,
    pub row: u8,
    pub row_span: u8,
}

struct AxisSegment {
    origin: u32,
    length: u32,
}

fn clamp_u8(value: u8, min: u8, max: u8) -> u8 {
    if value < min {
        min
    } else if value > max {
        max
    } else {
        value
    }
}

fn content_size(size: u32) -> u32 {
    size.saturating_sub(2 * FRAME_PADDING)
}

fn track_offset(track: u32, content: u32, tracks: u32) -> u32 {
    let num = track * (content + GUTTER);
    (2 * num + tracks) / (2 * tracks)
}

fn resolve_axis(track: u32, span: u32, size: u32, tracks: u32) -> AxisSegment {
    let content = content_size(size);
    let start = track_offset(track, content, tracks);
    let end = track_offset(track + span, content, tracks).saturating_sub(GUTTER);
    AxisSegment {
        origin: FRAME_PADDING + start,
        length: end.saturating_sub(start).max(1),
    }
}

pub fn clamp_placement(placement: GridPlacement) -> GridPlacement {
    let col_span = clamp_u8(placement.col_span, 1, COLUMNS);
    let row_span = clamp_u8(placement.row_span, 1, ROWS);
    let col = clamp_u8(placement.col, 0, COLUMNS - col_span);
    let row = clamp_u8(placement.row, 0, ROWS - row_span);
    GridPlacement {
        col,
        col_span,
        row,
        row_span,
    }
}

pub fn resolve_grid_rect(placement: GridPlacement, area_w: u16, area_h: u16) -> GridRect {
    let p = clamp_placement(placement);
    let cols = resolve_axis(
        p.col as u32,
        p.col_span as u32,
        area_w as u32,
        COLUMNS as u32,
    );
    let rows = resolve_axis(p.row as u32, p.row_span as u32, area_h as u32, ROWS as u32);
    GridRect {
        x: cols.origin as i16,
        y: rows.origin as i16,
        w: cols.length as i16,
        h: rows.length as i16,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn place(col: u8, col_span: u8, row: u8, row_span: u8) -> GridPlacement {
        GridPlacement {
            col,
            col_span,
            row,
            row_span,
        }
    }

    #[test]
    fn golden_columns_320_wide() {
        assert_eq!(resolve_grid_rect(place(0, 1, 0, 1), 320, 240).w, 13);
        assert_eq!(resolve_grid_rect(place(0, 6, 0, 1), 320, 240).w, 138);
        assert_eq!(resolve_grid_rect(place(0, 6, 0, 1), 320, 240).x, 16);
        assert_eq!(resolve_grid_rect(place(6, 6, 0, 1), 320, 240).x, 166);
        assert_eq!(resolve_grid_rect(place(6, 6, 0, 1), 320, 240).w, 138);
        assert_eq!(resolve_grid_rect(place(0, 12, 0, 1), 320, 240).w, 288);
        assert_eq!(resolve_grid_rect(place(0, 12, 0, 1), 320, 240).x, 16);
    }

    #[test]
    fn golden_column_pitch_320_wide() {
        let a = resolve_grid_rect(place(0, 1, 0, 1), 320, 240).x;
        let b = resolve_grid_rect(place(1, 1, 0, 1), 320, 240).x;
        assert_eq!(b - a, 25);
    }

    #[test]
    fn golden_rows_224_tall() {
        assert_eq!(resolve_grid_rect(place(0, 1, 0, 3), 320, 224).h, 39);
        assert_eq!(resolve_grid_rect(place(0, 1, 0, 6), 320, 224).h, 90);
        assert_eq!(resolve_grid_rect(place(0, 1, 0, 6), 320, 224).y, 16);
        assert_eq!(resolve_grid_rect(place(0, 1, 6, 6), 320, 224).y, 118);
    }

    #[test]
    fn golden_row_pitch_224_tall() {
        let a = resolve_grid_rect(place(0, 1, 0, 1), 320, 224).y;
        let b = resolve_grid_rect(place(0, 1, 1, 1), 320, 224).y;
        assert_eq!(b - a, 17);
    }

    #[test]
    fn degenerate_area_clamps_length_to_min_1px() {
        let rect = resolve_grid_rect(place(0, 1, 0, 1), 0, 0);
        assert_eq!(rect.w, 1);
        assert_eq!(rect.h, 1);
    }

    #[test]
    fn garbage_placement_is_clamped_into_grid() {
        let clamped = clamp_placement(place(200, 200, 200, 200));
        assert_eq!(clamped.col_span, COLUMNS);
        assert_eq!(clamped.row_span, ROWS);
        assert_eq!(clamped.col, 0);
        assert_eq!(clamped.row, 0);

        let zero_span = clamp_placement(place(5, 0, 5, 0));
        assert_eq!(zero_span.col_span, 1);
        assert_eq!(zero_span.row_span, 1);
        assert_eq!(zero_span.col, 5);
        assert_eq!(zero_span.row, 5);
    }

    fn assert_inside_frame(area_w: u16, area_h: u16) {
        let frame = FRAME_PADDING as i16;
        let right = area_w as i16 - frame;
        let bottom = area_h as i16 - frame;
        for col_span in 1..=COLUMNS {
            for col in 0..=(COLUMNS - col_span) {
                for row_span in 1..=ROWS {
                    for row in 0..=(ROWS - row_span) {
                        let rect =
                            resolve_grid_rect(place(col, col_span, row, row_span), area_w, area_h);
                        assert!(rect.x >= frame);
                        assert!(rect.y >= frame);
                        assert!(rect.w >= 1);
                        assert!(rect.h >= 1);
                        assert!(rect.x + rect.w <= right);
                        assert!(rect.y + rect.h <= bottom);
                    }
                }
            }
        }
    }

    #[test]
    fn every_valid_placement_stays_inside_frame() {
        assert_inside_frame(320, 240);
        assert_inside_frame(320, 224);
    }

    #[test]
    fn garbage_placement_never_resolves_outside_frame() {
        let area_w = 320;
        let area_h = 224;
        let rect = resolve_grid_rect(place(250, 250, 250, 250), area_w, area_h);
        assert!(rect.x >= FRAME_PADDING as i16);
        assert!(rect.y >= FRAME_PADDING as i16);
        assert!(rect.x + rect.w <= area_w as i16 - FRAME_PADDING as i16);
        assert!(rect.y + rect.h <= area_h as i16 - FRAME_PADDING as i16);
    }
}
