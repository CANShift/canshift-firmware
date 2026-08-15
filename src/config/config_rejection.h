#pragma once

#include "config_types.h"

#include <stdint.h>

struct CfgRejection {
    bool present;
    uint8_t pageNumber;
    char widgetId[CFG_MAX_ID_LEN];
    int16_t widgetY;
    int16_t widgetH;
    int16_t maxY;
};
