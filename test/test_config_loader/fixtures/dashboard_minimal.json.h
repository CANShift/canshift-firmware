#pragma once
// dashboard_minimal.json.h — embedded JSON fixtures for config_loader tests.
//
// Two payloads:
//   - kDashboardMinimal: schema major 1 → matches the firmware build, parses
//     cleanly, populates a single page with no widgets.
//   - kDashboardWrongVersion: schema major 99 → triggers the "VER_MISMATCH"
//     warning path. The loader still parses successfully and exposes the
//     content; the mismatch is observable through the version field on the
//     loaded struct.

namespace fixtures {

constexpr const char *kDashboardMinimal = R"({
  "version": "1.0.0",
  "name": "Minimal Test Dashboard",
  "defaultPageId": "main",
  "revLimitRpm": 7000,
  "topBar": {
    "height": 24,
    "bgColor": "#111111",
    "textColor": "#FFFFFF"
  },
  "pages": [
    {
      "id": "main",
      "backgroundColor": "#1A1A1A",
      "showTopBar": true,
      "widgets": []
    }
  ]
})";

constexpr const char *kDashboardWrongVersion = R"({
  "version": "99.0.0",
  "name": "Wrong Version",
  "defaultPageId": "main",
  "revLimitRpm": 7000,
  "topBar": {"height": 24, "bgColor": "#111111", "textColor": "#FFFFFF"},
  "pages": [
    {"id": "main", "backgroundColor": "#000000", "showTopBar": true, "widgets": []}
  ]
})";

} // namespace fixtures
