#pragma once
// json_reader.h — Single-instantiation ArduinoJson parse wrapper.
//
// ArduinoJson's `deserializeJson()` is a function template that emits a fresh
// `JsonDeserializer<Reader, Filter>::parseVariant<...>` instantiation for every
// distinct reader/filter pair across the binary. The audit in #305 / #406
// found ~28 KB of flash burned on five near-identical instantiations triggered
// by call sites passing `const char*`, `char*`, `const char* + size_t` and a
// filtered variant.
//
// Routing every call site through these wrappers collapses the parser down to
// one instantiation per filter mode (one with no filter, one with the cmd
// filter), saving ~10 KB of flash. Behaviour and error semantics are
// preserved: each helper returns ArduinoJson's `DeserializationError` exactly
// as the underlying call would.

#include <ArduinoJson.h>
#include <stddef.h>

namespace JsonReader {

// Parse `len` bytes from `data` into `doc` using ArduinoJson's bounded reader.
// `data` does not need to be null-terminated. Errors propagate verbatim.
DeserializationError parse(JsonDocument &doc, const char *data, size_t len);

// Filtered variant — same reader path, but only fields kept by `filter` are
// materialised in `doc`. Used by the USB command peek to read just `cmd`
// without loading the full PUT_CONFIG payload.
DeserializationError parseFiltered(JsonDocument &doc, const char *data, size_t len,
                                   JsonDocument &filter);

} // namespace JsonReader
