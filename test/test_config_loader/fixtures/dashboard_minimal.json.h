#pragma once

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

constexpr const char *kDashboardCorrupt = R"({
  "version": "1.0.0",
  "name": "Corrupt",
  "defaultPageId": "main",
  "pages": [
    {"id": "main"
)";

constexpr const char *kDashboardWithProfile = R"({
  "version": "1.0.0",
  "name": "Profile Test",
  "defaultPageId": "main",
  "revLimitRpm": 7000,
  "targetProfile": "crowpanel-28",
  "topBar": {"height": 24, "bgColor": "#111111", "textColor": "#FFFFFF"},
  "pages": [
    {"id": "main", "backgroundColor": "#111111", "showTopBar": true, "widgets": []}
  ]
})";

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

constexpr const char *kDashboardLayoutOutOfRange = R"({
  "version": "1.0.0",
  "name": "Layout Clamp Test",
  "defaultPageId": "main",
  "revLimitRpm": 7000,
  "topBar": {"height": 24, "bgColor": "#111111", "textColor": "#FFFFFF"},
  "pages": [
    {"id": "main", "backgroundColor": "#111111", "showTopBar": true, "widgets": [
      {"id": "wNeg", "type": "label", "signal": "rpm", "style": {},
       "config": {"decimalPlaces": 0},
       "layout": {"col": -5, "colSpan": 3, "row": -1, "rowSpan": 2, "zOrder": 0}},
      {"id": "wHuge", "type": "label", "signal": "rpm", "style": {},
       "config": {"decimalPlaces": 0},
       "layout": {"col": 200, "colSpan": 3, "row": 300, "rowSpan": 2, "zOrder": 0}},
      {"id": "wZeroSpan", "type": "label", "signal": "rpm", "style": {},
       "config": {"decimalPlaces": 0},
       "layout": {"col": 4, "colSpan": 0, "row": 4, "rowSpan": 0, "zOrder": 0}},
      {"id": "wOverflow", "type": "label", "signal": "rpm", "style": {},
       "config": {"decimalPlaces": 0},
       "layout": {"col": 8, "colSpan": 10, "row": 11, "rowSpan": 4, "zOrder": 0}}
    ]}
  ]
})";

constexpr const char *kDashboardWithThemes = R"({
  "version": "1.0.0",
  "name": "Theme Test",
  "defaultPageId": "main",
  "revLimitRpm": 7000,
  "topBar": {"height": 24, "bgColor": "#111111", "textColor": "#FFFFFF"},
  "theme": {
    "day": {
      "bgColor": "#E8E6E1",
      "palette": {
        "surface": "#F5F3EF", "primary": "#C41200", "accent": "#B35C00",
        "text": "#141414", "textDim": "#6A665F",
        "warning": "#B35C00", "danger": "#C41200", "success": "#006622"
      }
    },
    "night": {"bgColor": "#121212"}
  },
  "dayNightSignal": "flag_headlights",
  "pages": [
    {"id": "main", "backgroundColor": "#1A1A1A", "showTopBar": true, "widgets": []}
  ]
})";

constexpr const char *kDashboardThemeEdgeCases = R"({
  "version": "1.0.0",
  "name": "Theme Edge Cases",
  "defaultPageId": "main",
  "revLimitRpm": 7000,
  "topBar": {"height": 24, "bgColor": "#111111", "textColor": "#FFFFFF"},
  "theme": {
    "day": {"bgColor": "#E8E6E1", "palette": {"text": "#141414"}},
    "night": {"bgColor": "#121212", "palette": {"surface": "#1E1E1E"}}
  },
  "dayNightSignal": "signal_name_that_is_way_too_long_for_the_cap",
  "pages": [
    {"id": "main", "backgroundColor": "#1A1A1A", "showTopBar": true, "widgets": []}
  ]
})";

constexpr const char *kDashboardGearBig = R"({
  "version": "1.42.0",
  "name": "Gear Big",
  "defaultPageId": "main",
  "pages": [
    {"id": "main", "widgets": [
      {"id": "gear", "type": "gear", "signal": "gear", "style": {},
       "config": {"type": "gear", "big": 96},
       "layout": {"col": 0, "colSpan": 6, "row": 0, "rowSpan": 6}}
    ]}
  ]
})";

constexpr const char *kDashboardOverflow = R"({
  "version": "1.42.0",
  "name": "Overflow",
  "defaultPageId": "main",
  "topBar": {"height": 250},
  "pages": [
    {"id": "main", "showTopBar": true, "widgets": [
      {"id": "rpm", "type": "label", "signal": "rpm", "style": {}, "config": {},
       "layout": {"col": 0, "colSpan": 6, "row": 0, "rowSpan": 6}}
    ]}
  ]
})";

constexpr const char *kDashboardPreGrid = R"({
  "version": "1.24.0",
  "name": "Pre Grid",
  "defaultPageId": "main",
  "pages": [{"id": "main", "widgets": []}]
})";

constexpr const char *kDashboardGridFloor = R"({
  "version": "1.25.0",
  "name": "Grid Floor",
  "defaultPageId": "main",
  "pages": [{"id": "main", "widgets": []}]
})";

}
