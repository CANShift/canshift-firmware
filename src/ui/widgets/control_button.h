#pragma once

#include "control_state_rs.h"
#include "ui/control_vocabulary.h"

#include <lvgl.h>
#include <stdint.h>

namespace ControlButton {

struct Surface {
    lv_obj_t *box;
    lv_obj_t *kicker;
    lv_obj_t *word;
    lv_obj_t *segments[CONTROL_STEP_MAX];
    bool hasSegments;
    ControlVocabulary::ControlState painted;
    uint8_t paintedLevel;
    bool everPainted;
};

Surface build(lv_obj_t *box, bool withSegments);

void setTexts(const Surface &surface, const char *kicker, const char *word);

void paint(Surface &surface, ControlVocabulary::ControlState state, uint8_t level);

} // namespace ControlButton
