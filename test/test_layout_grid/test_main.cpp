#include "layout_grid_rs.h"
#include "layout_scale.h"

#include <unity.h>

namespace {

constexpr uint16_t FRAME = 8;

LayoutGridRectRs resolve(uint8_t col, uint8_t colSpan, uint8_t row, uint8_t rowSpan, uint16_t areaW,
                         uint16_t areaH) {
    LayoutGridRectRs rect = {};
    layout_grid_resolve_rs(col, colSpan, row, rowSpan, areaW, areaH, &rect);
    return rect;
}

void test_columns_320wide() {
    TEST_ASSERT_EQUAL_INT16(20, resolve(0, 1, 0, 1, 320, 240).w);
    TEST_ASSERT_EQUAL_INT16(149, resolve(0, 6, 0, 1, 320, 240).w);
    TEST_ASSERT_EQUAL_INT16(8, resolve(0, 6, 0, 1, 320, 240).x);
    TEST_ASSERT_EQUAL_INT16(163, resolve(6, 6, 0, 1, 320, 240).x);
    TEST_ASSERT_EQUAL_INT16(149, resolve(6, 6, 0, 1, 320, 240).w);
    TEST_ASSERT_EQUAL_INT16(304, resolve(0, 12, 0, 1, 320, 240).w);
    TEST_ASSERT_EQUAL_INT16(8, resolve(0, 12, 0, 1, 320, 240).x);
}

void test_columnPitch_320wide() {
    const int16_t a = resolve(0, 1, 0, 1, 320, 240).x;
    const int16_t b = resolve(1, 1, 0, 1, 320, 240).x;
    TEST_ASSERT_EQUAL_INT16(26, b - a);
}

void test_rows_224tall() {
    TEST_ASSERT_EQUAL_INT16(48, resolve(0, 1, 0, 3, 320, 224).h);
    TEST_ASSERT_EQUAL_INT16(101, resolve(0, 1, 0, 6, 320, 224).h);
    TEST_ASSERT_EQUAL_INT16(8, resolve(0, 1, 0, 6, 320, 224).y);
    TEST_ASSERT_EQUAL_INT16(115, resolve(0, 1, 6, 6, 320, 224).y);
}

void test_rowPitch_224tall() {
    const int16_t a = resolve(0, 1, 0, 1, 320, 224).y;
    const int16_t b = resolve(0, 1, 1, 1, 320, 224).y;
    TEST_ASSERT_EQUAL_INT16(18, b - a);
}

void test_degenerateArea_clampsToMin1px() {
    const LayoutGridRectRs rect = resolve(0, 1, 0, 1, 0, 0);
    TEST_ASSERT_EQUAL_INT16(1, rect.w);
    TEST_ASSERT_EQUAL_INT16(1, rect.h);
}

void test_garbagePlacement_staysInsideFrame() {
    const LayoutGridRectRs rect = resolve(250, 250, 250, 250, 320, 224);
    TEST_ASSERT_TRUE(rect.x >= FRAME);
    TEST_ASSERT_TRUE(rect.y >= FRAME);
    TEST_ASSERT_TRUE(rect.x + rect.w <= 320 - FRAME);
    TEST_ASSERT_TRUE(rect.y + rect.h <= 224 - FRAME);
}

void assertInsideFrame(uint16_t areaW, uint16_t areaH) {
    const int16_t right = static_cast<int16_t>(areaW - FRAME);
    const int16_t bottom = static_cast<int16_t>(areaH - FRAME);
    for (uint8_t colSpan = 1; colSpan <= 12; ++colSpan) {
        for (uint8_t col = 0; col <= 12 - colSpan; ++col) {
            for (uint8_t rowSpan = 1; rowSpan <= 12; ++rowSpan) {
                for (uint8_t row = 0; row <= 12 - rowSpan; ++row) {
                    const LayoutGridRectRs rect = resolve(col, colSpan, row, rowSpan, areaW, areaH);
                    TEST_ASSERT_TRUE(rect.x >= FRAME);
                    TEST_ASSERT_TRUE(rect.y >= FRAME);
                    TEST_ASSERT_TRUE(rect.w >= 1);
                    TEST_ASSERT_TRUE(rect.h >= 1);
                    TEST_ASSERT_TRUE(rect.x + rect.w <= right);
                    TEST_ASSERT_TRUE(rect.y + rect.h <= bottom);
                }
            }
        }
    }
}

void test_everyValidPlacement_staysInsideFrame() {
    assertInsideFrame(320, 240);
    assertInsideFrame(320, 224);
}

void test_scale_ffi() {
    TEST_ASSERT_EQUAL_INT32(32, layout_scale_rs(32, 240, 240));
    TEST_ASSERT_EQUAL_INT32(64, layout_scale_rs(32, 240, 480));
    TEST_ASSERT_EQUAL_INT32(66, layout_scale_rs(44, 320, 480));
    TEST_ASSERT_EQUAL_INT32(0, layout_scale_rs(100, 0, 480));
    TEST_ASSERT_EQUAL_INT32(-2, layout_scale_rs(-1, 2, 3));
}

void test_layoutScale_wrapper_identity_at_design() {
    TEST_ASSERT_EQUAL_INT16(44, LayoutScale::x(44));
    TEST_ASSERT_EQUAL_INT16(32, LayoutScale::y(32));
    TEST_ASSERT_EQUAL_INT16(36, LayoutScale::square(36));
}

} // namespace

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_columns_320wide);
    RUN_TEST(test_columnPitch_320wide);
    RUN_TEST(test_rows_224tall);
    RUN_TEST(test_rowPitch_224tall);
    RUN_TEST(test_degenerateArea_clampsToMin1px);
    RUN_TEST(test_garbagePlacement_staysInsideFrame);
    RUN_TEST(test_everyValidPlacement_staysInsideFrame);
    RUN_TEST(test_scale_ffi);
    RUN_TEST(test_layoutScale_wrapper_identity_at_design);
    return UNITY_END();
}
