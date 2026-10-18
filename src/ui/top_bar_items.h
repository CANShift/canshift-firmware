#pragma once

#include "config/config_types.h"

#include <lvgl.h>
#include <stdint.h>

namespace TopBarItems {

using Builder = lv_obj_t *(*)(const CfgTopBarItem &item, bool hasTheme);

struct KindOps {
    TopBarItemKind kind;
    Builder build;
    bool gapped;
    bool tracked;
};

const KindOps *opsFor(TopBarItemKind kind);
int16_t derivedGap(int16_t height);
int16_t derivedPadding(int16_t height);

}
