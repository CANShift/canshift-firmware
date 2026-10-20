#pragma once

#include "board_profile.h"

#include <ArduinoJson.h>
#include <stddef.h>

namespace canshift::boards {

bool readString(JsonVariantConst v, char *buf, size_t cap);
bool subObject(JsonObjectConst parent, const char *key, JsonObjectConst &out);
bool parseChipFamily(JsonVariantConst v, ChipFamily &out);
bool parseProfileSections(JsonObjectConst profile, BoardProfile &out);

}
