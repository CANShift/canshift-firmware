#pragma once

namespace fixtures {

constexpr const char *kStatusRowImperial = R"({
  "version": "1.0.0",
  "name": "Status row",
  "defaultPageId": "timing",
  "revLimitRpm": 7000,
  "units": "imperial",
  "topBar": {"height": 24, "bgColor": "#111111", "textColor": "#FFFFFF"},
  "pages": [
    {"id": "timing", "backgroundColor": "#1A1A1A", "showTopBar": true, "widgets": [],
     "statusRow": {
       "center": {"type": "timer", "source": "lapCount", "text": "LAP", "position": "center"},
       "right": {"type": "timer", "source": "best", "text": "BEST", "position": "right"}}},
    {"id": "controls", "backgroundColor": "#1A1A1A", "showTopBar": true, "widgets": [],
     "statusRow": {
       "center": {"type": "flagWord", "signal": "flag_anti_lag", "text": "ALS ON",
                  "offText": "ALS OFF", "position": "center"},
       "right": {"type": "signal", "signal": "trip_km", "format": "TRIP %.0f", "unit": "km",
                 "showUnit": true, "position": "right"}}}
  ]
})";

constexpr const char *kStatusRowMetric = R"({
  "version": "1.0.0",
  "name": "Status row metric",
  "defaultPageId": "main",
  "revLimitRpm": 7000,
  "units": "metric",
  "topBar": {"height": 24, "bgColor": "#111111", "textColor": "#FFFFFF"},
  "pages": [
    {"id": "main", "backgroundColor": "#1A1A1A", "showTopBar": true, "widgets": []}
  ]
})";

}
