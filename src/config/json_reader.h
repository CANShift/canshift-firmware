#pragma once

#include <ArduinoJson.h>
#include <stddef.h>

namespace JsonReader {

ArduinoJson::Allocator *configAllocator();

DeserializationError parse(JsonDocument &doc, const char *data, size_t len);

DeserializationError parseFiltered(JsonDocument &doc, const char *data, size_t len,
                                   JsonDocument &filter);

} // namespace JsonReader
