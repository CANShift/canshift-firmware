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

// Same schema as kDashboardMinimal but with a distinct name + a second page,
// so reloadAll() can be observed switching from one to the other.
constexpr const char *kDashboardMinimalReload = R"({
  "version": "1.0.0",
  "name": "Reloaded Dashboard",
  "defaultPageId": "second",
  "revLimitRpm": 7500,
  "topBar": {
    "height": 24,
    "bgColor": "#111111",
    "textColor": "#FFFFFF"
  },
  "pages": [
    {
      "id": "first",
      "backgroundColor": "#1A1A1A",
      "showTopBar": true,
      "widgets": []
    },
    {
      "id": "second",
      "backgroundColor": "#202020",
      "showTopBar": true,
      "widgets": []
    }
  ]
})";

// Truncated JSON — parse must fail. Used to assert reloadAll() returns false.
constexpr const char *kDashboardCorrupt = R"({
  "version": "1.0.0",
  "name": "Corrupt",
  "defaultPageId": "main",
  "pages": [
    {"id": "main"
)";

// Two pages exercising the page template field (issue #451) — the first omits
// `template` (must default to CUSTOM, back-compat), the second carries
// `cruise_control` (must round-trip to CfgPageTemplate::CRUISE_CONTROL).
constexpr const char *kDashboardWithTemplates = R"({
  "version": "1.0.0",
  "name": "Template Test",
  "defaultPageId": "p1",
  "revLimitRpm": 7000,
  "topBar": {"height": 24, "bgColor": "#111111", "textColor": "#FFFFFF"},
  "pages": [
    {"id": "p1", "backgroundColor": "#111111", "showTopBar": true, "widgets": []},
    {"id": "p2", "backgroundColor": "#222222", "showTopBar": true,
     "template": "cruise_control", "widgets": []}
  ]
})";

} // namespace fixtures
