use crate::{resolve_grid_rect, scale, GridPlacement, GridRect};

const _: () = assert!(core::mem::size_of::<GridRect>() == 8);

#[no_mangle]
pub extern "C" fn layout_scale_rs(value: i32, from_extent: u16, to_extent: u16) -> i32 {
    scale(value, from_extent, to_extent)
}

#[no_mangle]
pub unsafe extern "C" fn layout_grid_resolve_rs(
    col: u8,
    col_span: u8,
    row: u8,
    row_span: u8,
    area_w: u16,
    area_h: u16,
    out: *mut GridRect,
) {
    if out.is_null() {
        return;
    }
    let placement = GridPlacement {
        col,
        col_span,
        row,
        row_span,
    };
    unsafe { *out = resolve_grid_rect(placement, area_w, area_h) };
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn ffi_matches_golden() {
        let mut rect = GridRect::default();
        unsafe {
            layout_grid_resolve_rs(6, 6, 0, 1, 320, 240, &mut rect);
        }
        assert_eq!(rect.x, 166);
        assert_eq!(rect.w, 138);
    }

    #[test]
    fn ffi_null_out_is_noop() {
        unsafe {
            layout_grid_resolve_rs(0, 1, 0, 1, 320, 240, core::ptr::null_mut());
        }
    }

    #[test]
    fn ffi_scale_identity_and_upscale() {
        assert_eq!(layout_scale_rs(32, 240, 240), 32);
        assert_eq!(layout_scale_rs(32, 240, 480), 64);
        assert_eq!(layout_scale_rs(44, 320, 480), 66);
    }
}
