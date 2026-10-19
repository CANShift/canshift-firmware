#pragma once

namespace fixtures {

constexpr const char *kSignalsMinimal = R"({
  "version": "1.0.0",
  "protocol": "custom_v1.0",
  "canSpeedKbps": 500,
  "signals": [
    {
      "name": "rpm",
      "canFrameId": "0x370",
      "startByte": 0,
      "byteLength": 2,
      "bigEndian": true,
      "signed": false,
      "scale": 1.0,
      "offset": 0.0,
      "unit": "rpm",
      "min": 0,
      "max": 8000,
      "timeoutMs": 1000
    }
  ]
})";

constexpr const char *kSignalsCorrupt = R"({
  "version": "1.0.0",
  "protocol": "custom_v1.0",
  "signals": [
    {"name": "rpm"
)";

constexpr const char *kSignalsMalformedCanFrameId = R"({
  "version": "1.0.0",
  "protocol": "custom_v1.0",
  "canSpeedKbps": 500,
  "signals": [
    {
      "name": "rpm",
      "canFrameId": "0x370",
      "startByte": 0,
      "byteLength": 2,
      "bigEndian": true,
      "signed": false,
      "scale": 1.0,
      "offset": 0.0,
      "unit": "rpm",
      "min": 0,
      "max": 8000,
      "timeoutMs": 1000
    },
    {
      "name": "tps",
      "canFrameId": "0xGGG",
      "startByte": 0,
      "byteLength": 1,
      "bigEndian": true,
      "signed": false,
      "scale": 1.0,
      "offset": 0.0,
      "unit": "%",
      "min": 0,
      "max": 100,
      "timeoutMs": 1000
    }
  ]
})";

constexpr const char *kSignalsNameTooLong = R"({
  "version": "1.0.0",
  "protocol": "custom_v1.0",
  "canSpeedKbps": 500,
  "signals": [
    {
      "name": "rpm",
      "canFrameId": "0x370",
      "startByte": 0,
      "byteLength": 2,
      "bigEndian": true,
      "signed": false,
      "scale": 1.0,
      "offset": 0.0,
      "unit": "rpm",
      "min": 0,
      "max": 8000,
      "timeoutMs": 1000
    },
    {
      "name": "this_signal_name_is_deliberately_way_too_long_to_exceed_the_64_char_signal_buffer_cap",
      "canFrameId": "0x200",
      "startByte": 0,
      "byteLength": 1,
      "bigEndian": true,
      "signed": false,
      "scale": 1.0,
      "offset": 0.0,
      "unit": "%",
      "min": 0,
      "max": 100,
      "timeoutMs": 1000
    }
  ]
})";

constexpr const char *kSignalsUnitTooLong = R"({
  "version": "1.0.0",
  "protocol": "custom_v1.0",
  "canSpeedKbps": 500,
  "signals": [
    {
      "name": "rpm",
      "canFrameId": "0x370",
      "startByte": 0,
      "byteLength": 2,
      "bigEndian": true,
      "signed": false,
      "scale": 1.0,
      "offset": 0.0,
      "unit": "this_unit_string_is_way_too_long",
      "min": 0,
      "max": 8000,
      "timeoutMs": 1000
    }
  ]
})";

constexpr const char *kSignalsThreeByteValue = R"({
  "version": "1.0.0",
  "protocol": "custom_v1.0",
  "canSpeedKbps": 500,
  "signals": [
    {
      "name": "odo",
      "canFrameId": "0x1f5",
      "startByte": 0,
      "byteLength": 3,
      "bigEndian": true,
      "signed": false,
      "scale": 1.0,
      "offset": 0.0,
      "unit": "km",
      "min": 0,
      "max": 16777215,
      "timeoutMs": 1000
    },
    {
      "name": "too_wide",
      "canFrameId": "0x1f6",
      "startByte": 0,
      "byteLength": 5,
      "bigEndian": true,
      "signed": false,
      "scale": 1.0,
      "offset": 0.0,
      "unit": "",
      "min": 0,
      "max": 100,
      "timeoutMs": 1000
    }
  ]
})";

}
