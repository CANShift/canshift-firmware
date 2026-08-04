
#ifndef CANSHIFT_LAYOUT_GRID_RS_H
#define CANSHIFT_LAYOUT_GRID_RS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct LayoutGridRectRs {
    int16_t x;
    int16_t y;
    int16_t w;
    int16_t h;
} LayoutGridRectRs;

void layout_grid_resolve_rs(uint8_t col, uint8_t col_span, uint8_t row, uint8_t row_span,
                            uint16_t area_w, uint16_t area_h, LayoutGridRectRs *out);

int32_t layout_scale_rs(int32_t value, uint16_t from_extent, uint16_t to_extent);

#ifdef __cplusplus
}

static_assert(sizeof(LayoutGridRectRs) == 8, "LayoutGridRectRs layout must match rust/layout-grid");
#endif

#endif
