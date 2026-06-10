#include "json_reader.h"

namespace JsonReader {

DeserializationError parse(JsonDocument &doc, const char *data, size_t len) {
    return deserializeJson(doc, data, len);
}

DeserializationError parseFiltered(JsonDocument &doc, const char *data, size_t len,
                                   JsonDocument &filter) {
    return deserializeJson(doc, data, len, DeserializationOption::Filter(filter));
}

} // namespace JsonReader
