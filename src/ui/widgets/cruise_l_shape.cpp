#include "cruise_l_shape.h"

#include <math.h>

namespace CruiseLShape {

namespace {

constexpr uint8_t kBezierSegments = 4;

bool isRightColumn(Corner corner) {
    return corner == Corner::kTR || corner == Corner::kBR;
}

bool isBottomRow(Corner corner) {
    return corner == Corner::kBL || corner == Corner::kBR;
}

lv_point_t bezierAt(const lv_point_t &p0, const lv_point_t &pc, const lv_point_t &p2, float t) {
    const float omt = 1.0f - t;
    return {
        static_cast<lv_coord_t>(lroundf(omt * omt * p0.x + 2.0f * omt * t * pc.x + t * t * p2.x)),
        static_cast<lv_coord_t>(lroundf(omt * omt * p0.y + 2.0f * omt * t * pc.y + t * t * p2.y))};
}

struct PathBuilder {
    lv_point_t *pts;
    uint8_t n;
    int16_t ox;
    int16_t oy;
    int16_t w;
    int16_t h;
    bool mirrorX;
    bool mirrorY;

    lv_point_t map(int16_t px, int16_t py) const {
        const int16_t tx = mirrorX ? static_cast<int16_t>(w - px) : px;
        const int16_t ty = mirrorY ? static_cast<int16_t>(h - py) : py;
        return {static_cast<lv_coord_t>(ox + tx), static_cast<lv_coord_t>(oy + ty)};
    }

    void vertex(int16_t px, int16_t py) {
        pts[n++] = map(px, py);
    }

    void corner(int16_t x0, int16_t y0, int16_t xc, int16_t yc, int16_t x2, int16_t y2) {
        const lv_point_t p0 = map(x0, y0);
        const lv_point_t pc = map(xc, yc);
        const lv_point_t p2 = map(x2, y2);
        for (uint8_t i = 1; i <= kBezierSegments; ++i) {
            pts[n++] = bezierAt(p0, pc, p2, static_cast<float>(i) / kBezierSegments);
        }
    }
};

} // namespace

uint8_t buildOutline(lv_point_t *pts, const lv_area_t &area, Corner corner, const Geometry &geom) {
    const int16_t w = static_cast<int16_t>(area.x2 - area.x1 + 1);
    const int16_t h = static_cast<int16_t>(area.y2 - area.y1 + 1);
    const int16_t r = geom.radius;
    const int16_t ir = geom.innerRadius;
    const int16_t nW = geom.notchW;
    const int16_t nH = geom.notchH;

    PathBuilder b{pts, 0, area.x1, area.y1, w, h, isRightColumn(corner), isBottomRow(corner)};
    b.vertex(r, 0);
    b.vertex(static_cast<int16_t>(w - r), 0);
    b.corner(w - r, 0, w, 0, w, r);
    b.vertex(w, static_cast<int16_t>(h - nH - ir));
    b.corner(w, h - nH - ir, w, h - nH, w - ir, h - nH);
    b.vertex(static_cast<int16_t>(w - nW + ir), static_cast<int16_t>(h - nH));
    b.corner(w - nW + ir, h - nH, w - nW, h - nH, w - nW, h - nH + ir);
    b.vertex(static_cast<int16_t>(w - nW), static_cast<int16_t>(h - ir));
    b.corner(w - nW, h - ir, w - nW, h, w - nW - ir, h);
    b.vertex(r, h);
    b.corner(r, h, 0, h, 0, h - r);
    b.vertex(0, r);
    b.corner(0, r, 0, 0, r, 0);
    return b.n;
}

void buildFillArms(const lv_area_t &btn, Corner corner, const Geometry &geom, lv_area_t *armH,
                   lv_area_t *armV) {
    const int16_t w = static_cast<int16_t>(btn.x2 - btn.x1 + 1);
    const int16_t h = static_cast<int16_t>(btn.y2 - btn.y1 + 1);
    const bool mx = isRightColumn(corner);
    const bool my = isBottomRow(corner);
    *armH = {btn.x1, static_cast<lv_coord_t>(btn.y1 + (my ? geom.notchH : 0)),
             static_cast<lv_coord_t>(btn.x1 + w - 1),
             static_cast<lv_coord_t>(btn.y1 + (my ? h - 1 : h - geom.notchH - 1))};
    *armV = {static_cast<lv_coord_t>(btn.x1 + (mx ? geom.notchW : 0)), btn.y1,
             static_cast<lv_coord_t>(btn.x1 + (mx ? w - 1 : w - geom.notchW - 1)),
             static_cast<lv_coord_t>(btn.y1 + h - 1)};
}

bool hitInNotch(const lv_area_t &area, Corner corner, const Geometry &geom,
                const lv_point_t &point) {
    const int16_t px = static_cast<int16_t>(point.x - area.x1);
    const int16_t py = static_cast<int16_t>(point.y - area.y1);
    const int16_t w = static_cast<int16_t>(area.x2 - area.x1 + 1);
    const int16_t h = static_cast<int16_t>(area.y2 - area.y1 + 1);
    const bool inNotchX = isRightColumn(corner) ? (px < geom.notchW) : (px >= w - geom.notchW);
    const bool inNotchY = isBottomRow(corner) ? (py < geom.notchH) : (py >= h - geom.notchH);
    return inNotchX && inNotchY;
}

} // namespace CruiseLShape
