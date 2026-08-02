#include "brand_mark.h"
#include "monogram_baked.h"

lv_obj_t *BrandMark::create(lv_obj_t *parent, bool mark) {
    lv_obj_t *img = lv_img_create(parent);
    if (!img)
        return nullptr;
    lv_img_set_src(img, mark ? MonogramBaked::mark() : MonogramBaked::header());
    return img;
}
