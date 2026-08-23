#pragma once

namespace fixtures {

constexpr const char *kDashboardImperial = R"({
  "version": "1.0.0",
  "name": "Imperial",
  "defaultPageId": "main",
  "revLimitRpm": 7000,
  "units": "imperial",
  "topBar": {"height": 24, "bgColor": "#111111", "textColor": "#FFFFFF"},
  "pages": [
    {"id": "main", "backgroundColor": "#1A1A1A", "showTopBar": true, "widgets": []}
  ]
})";

constexpr const char *kDashboardMetric = R"({
  "version": "1.0.0",
  "name": "Metric",
  "defaultPageId": "main",
  "revLimitRpm": 7000,
  "units": "metric",
  "topBar": {"height": 24, "bgColor": "#111111", "textColor": "#FFFFFF"},
  "pages": [
    {"id": "main", "backgroundColor": "#1A1A1A", "showTopBar": true, "widgets": []}
  ]
})";

constexpr const char *kDashboardNoUnits = R"({
  "version": "1.0.0",
  "name": "No units field",
  "defaultPageId": "main",
  "revLimitRpm": 7000,
  "topBar": {"height": 24, "bgColor": "#111111", "textColor": "#FFFFFF"},
  "pages": [
    {"id": "main", "backgroundColor": "#1A1A1A", "showTopBar": true, "widgets": []}
  ]
})";

constexpr const char *kDashboardBogusUnits = R"({
  "version": "1.0.0",
  "name": "Bogus units field",
  "defaultPageId": "main",
  "revLimitRpm": 7000,
  "units": "furlongs",
  "topBar": {"height": 24, "bgColor": "#111111", "textColor": "#FFFFFF"},
  "pages": [
    {"id": "main", "backgroundColor": "#1A1A1A", "showTopBar": true, "widgets": []}
  ]
})";

} // namespace fixtures
