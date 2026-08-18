#pragma once

#include "board_profile.h"

#include "boards/crowpanel_28.h"
#include "boards/generic_esp32s3.h"
#include "boards/generic_ili9341.h"
#include "boards/generic_ili9341_gt911.h"
#include "boards/waveshare_s3_28.h"

#include <stddef.h>

namespace canshift::boards {

inline constexpr const BoardProfile *kCatalog[] = {
    &kCrowpanel28, &kGenericIli9341, &kGenericIli9341Gt911, &kGenericEsp32s3, &kWaveshareS328,
};

inline constexpr size_t kCatalogCount = sizeof(kCatalog) / sizeof(kCatalog[0]);

const BoardProfile *catalogBoard(const char *boardId, ChipFamily chipFamily);

} // namespace canshift::boards
