#pragma once
// signals_minimal.json.h — minimal signals.json fixture for config_loader
// tests. One signal mapped to CAN frame 0x370 (RPM, MaxxECU-style layout) is
// enough to verify the loader populates CfgSignalConfig.signals[0] without
// warnings.

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

// Truncated signals JSON — parse must fail. Used by issue #458 tests to assert
// reloadAll() leaves s_signals byte-identical when the signals file is bad.
constexpr const char *kSignalsCorrupt = R"({
  "version": "1.0.0",
  "protocol": "custom_v1.0",
  "signals": [
    {"name": "rpm"
)";

// Signals JSON with one malformed canFrameId. The good signal ("rpm") must
// survive; the bad signal ("tps") must be silently dropped. Used by
// test_loadSignals_malformedCanFrameId_dropsSignal (issue #1159).
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

// Signal with a name that exceeds CFG_MAX_SIGNAL_LEN (32). The signal must be
// dropped and the valid "rpm" signal before it must survive (#1162).
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
      "name": "this_signal_name_is_way_too_long_for_the_buffer",
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

// Signal with a unit string that exceeds sizeof(s.unit) (16). The signal must
// survive but its unit must be reset to "" (#1162).
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

} // namespace fixtures
