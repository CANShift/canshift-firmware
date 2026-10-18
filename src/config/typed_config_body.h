#pragma once

#include <ArduinoJson.h>

namespace TypedConfigBody {

constexpr char kDeviceConfigKey[] = "device_config";
constexpr char kInputBindingsKey[] = "input_bindings";

using BodyOf = JsonVariantConst (*)(const JsonDocument &doc);

inline JsonVariantConst device(const JsonDocument &doc) {
    const JsonVariantConst legacyWrapped = doc[kDeviceConfigKey];
    return legacyWrapped.is<JsonObjectConst>() ? legacyWrapped : doc.as<JsonVariantConst>();
}

inline JsonVariantConst inputBindings(const JsonDocument &doc) {
    return doc[kInputBindingsKey];
}

}
