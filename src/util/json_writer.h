#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <stddef.h>

namespace JsonWriter {

size_t toPrint(JsonVariantConst value, Print &out);

size_t measure(JsonVariantConst value);

size_t toBuffer(JsonVariantConst value, char *buffer, size_t capacity);

} // namespace JsonWriter
