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

} // namespace fixtures
