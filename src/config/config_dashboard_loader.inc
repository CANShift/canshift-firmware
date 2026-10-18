
#include "config_loader_internal.h"

#include "app_config.h"
#include "board_config.h"
#include "diag/error_store.h"
#include "diag/logger.h"

#include "json_reader.h"
#include "ui/dash_metrics.h"

#include <ArduinoJson.h>
#include <cstdint>
#include <cstring>
#include <new>

namespace ConfigLoaderInternal {

namespace {

using ConfigLoader::DashboardOutcome;

struct UnitSystemName {
    const char *name;
    CfgUnitSystem system;
};

constexpr UnitSystemName kUnitSystems[] = {
    {"metric", CfgUnitSystem::METRIC},
    {"imperial", CfgUnitSystem::IMPERIAL},
};

CfgUnitSystem parseUnitSystem(const char *raw) {
    for (const UnitSystemName &entry : kUnitSystems) {
        if (strcmp(raw, entry.name) == 0)
            return entry.system;
    }
    LOG_WARN("CFG", "dashboard.json: units '%s' unknown — rendering metric", raw);
    return CfgUnitSystem::METRIC;
}

void parseDashboardHeader(const JsonDocument &doc, CfgDashboard *out) {
    strlcpy(out->version, doc["version"] | "", sizeof(out->version));
    checkSchemaVersion("dashboard.json", out->version);
    checkDashboardSchemaFloor(out->version);
    strlcpy(out->name, doc["name"] | "", sizeof(out->name));
    strlcpy(out->defaultPageId, doc["defaultPageId"] | "", sizeof(out->defaultPageId));
    out->revLimitRpm = doc["revLimitRpm"] | 7200.0f;
    out->units = parseUnitSystem(doc["units"] | "metric");
    strlcpy(out->targetProfile, doc["targetProfile"] | "crowpanel-28", sizeof(out->targetProfile));
    const char *rawDayNightSignal = doc["dayNightSignal"] | "";
    if (strlcpy(out->dayNightSignal, rawDayNightSignal, sizeof(out->dayNightSignal)) >=
        sizeof(out->dayNightSignal)) {
        LOG_WARN("CFG", "dashboard.json: dayNightSignal too long — auto day/night disabled");
        out->dayNightSignal[0] = '\0';
    }
    parseCriticalAlerts(doc, out);
    parsePageButtons(doc["pageButtons"], &out->pageButtons);
}

void applyDefaultTopBarLayout(CfgTopBar *bar) {
    LOG_INFO("CFG", "topBar.layout missing — applying default layout");
    uint8_t &n = bar->itemCount;
    auto &items = bar->items;
    auto addItem = [&](TopBarItemKind kind, TopBarItemPos pos, const char *sig, const char *txt) {
        // NOLINTNEXTLINE(clang-analyzer-core.NullDereference)
        if (n >= kCfgMaxTopbarItems)
            return;
        CfgTopBarItem &out = items[n++];
        out.kind = kind;
        out.position = pos;
        strlcpy(out.signalId, sig, sizeof(out.signalId));
        strlcpy(out.text, txt, sizeof(out.text));
        out.format[0] = '\0';
    };
    addItem(TopBarItemKind::LABEL, TopBarItemPos::LEFT, "", "CAN");
    addItem(TopBarItemKind::CAN_RATE, TopBarItemPos::LEFT, "", "");
    addItem(TopBarItemKind::LABEL, TopBarItemPos::CENTER, "", "MAP");
    addItem(TopBarItemKind::SIGNAL, TopBarItemPos::CENTER, "map_number", "");
    if (n > 0)
        strlcpy(items[n - 1].format, "%.0f", sizeof(items[n - 1].format));
    addItem(TopBarItemKind::TRACK_BADGE, TopBarItemPos::RIGHT, "", "");
}

void parseTopBarLayout(JsonArrayConst layout, CfgTopBar *bar) {
    const size_t total = layout.size();
    if (total > kCfgMaxTopbarItems) {
        LOG_WARN("CFG", "topBar.layout: %u items exceed CFG_MAX_TOPBAR_ITEMS=%u — extras ignored",
                 static_cast<unsigned>(total), kCfgMaxTopbarItems);
    }
    for (JsonObjectConst item : layout) {
        if (bar->itemCount >= kCfgMaxTopbarItems)
            break;
        CfgTopBarItem tmp{};
        tmp.kind = parseTopBarItemKind(item["type"] | "");
        tmp.position = parseTopBarItemPos(item["position"] | "left");
        strlcpy(tmp.signalId, item["signal"] | "", kCfgMaxSignalLen);
        strlcpy(tmp.text, item["text"] | "", sizeof(tmp.text));
        strlcpy(tmp.format, item["format"] | "", sizeof(tmp.format));
        if (tmp.kind == TopBarItemKind::UNKNOWN) {
            LOG_WARN("CFG", "topBar.layout[%u]: unknown type — item dropped",
                     static_cast<unsigned>(bar->itemCount));
            continue;
        }
        bar->items[bar->itemCount++] = tmp;
    }
}

void parseTopBar(JsonObjectConst topBar, CfgTopBar *bar) {
    bar->height = topBar["height"] | 30;
    parseColor(topBar["bgColor"] | "#111111", &bar->bgColor);
    parseColor(topBar["textColor"] | "#AAAAAA", &bar->textColor);
    bar->itemCount = 0;
    JsonArrayConst layout = topBar["layout"];
    if (layout.isNull()) {
        applyDefaultTopBarLayout(bar);
        return;
    }
    parseTopBarLayout(layout, bar);
}

void parseThemeBlock(JsonObjectConst themeJson, const char *fallbackBg, CfgTheme *theme) {
    parseColor(themeJson["bgColor"] | fallbackBg, &theme->bgColor);
    JsonObjectConst paletteJson = themeJson["palette"];
    const bool hasText = paletteJson["text"].is<const char *>();
    const bool hasTextDim = paletteJson["textDim"].is<const char *>();
    theme->hasPalette = hasText && hasTextDim;
    if (theme->hasPalette) {
        parseColor(paletteJson["text"].as<const char *>(), &theme->text);
        parseColor(paletteJson["textDim"].as<const char *>(), &theme->textDim);
        return;
    }
    if (!paletteJson.isNull()) {
        LOG_WARN("CFG", "dashboard.json: theme palette missing text/textDim — ignored");
    }
}

void parseThemes(const JsonDocument &doc, CfgDashboard *out) {
    JsonObjectConst themeJson = doc["theme"];
    JsonObjectConst dayJson = themeJson["day"];
    JsonObjectConst nightJson = themeJson["night"];
    out->hasTheme = !dayJson.isNull() && !nightJson.isNull();
    if (!out->hasTheme) {
        if (!themeJson.isNull()) {
            LOG_WARN("CFG", "dashboard.json: theme is missing a day or night face — ignored");
        }
        return;
    }
    parseThemeBlock(dayJson, "#DDDDDD", &out->dayFace);
    parseThemeBlock(nightJson, "#1A1A1A", &out->nightFace);
}

bool parseStatusItem(JsonObjectConst item, CfgTopBarItem *out) {
    if (item.isNull())
        return false;
    CfgTopBarItem tmp{};
    tmp.kind = parseTopBarItemKind(item["type"] | "");
    if (tmp.kind == TopBarItemKind::UNKNOWN)
        return false;
    tmp.position = parseTopBarItemPos(item["position"] | "right");
    strlcpy(tmp.signalId, item["signal"] | "", kCfgMaxSignalLen);
    strlcpy(tmp.text, item["text"] | "", sizeof(tmp.text));
    strlcpy(tmp.format, item["format"] | "", sizeof(tmp.format));
    *out = tmp;
    return true;
}

void parsePageStatusRow(JsonObjectConst page, CfgPage *p) {
    p->statusRow.hasCenter = false;
    p->statusRow.hasRight = false;
    JsonObjectConst statusRow = page["statusRow"];
    if (statusRow.isNull())
        return;
    p->statusRow.hasCenter = parseStatusItem(statusRow["center"], &p->statusRow.center);
    p->statusRow.hasRight = parseStatusItem(statusRow["right"], &p->statusRow.right);
}

void parsePageMeta(JsonObjectConst page, CfgPage *p) {
    strlcpy(p->id, page["id"] | "", kCfgMaxIdLen);
    strlcpy(p->bgImagePath, page["backgroundImage"] | "", kCfgMaxPathLen);
    parseColor(page["backgroundColor"] | "#1A1A1A", &p->bgColor);
    p->showTopBar = page["showTopBar"] | true;
    parsePageStatusRow(page, p);
    p->visible = page["visible"] | true;
    const char *templateStr = page["template"] | "custom";
    if (strcmp(templateStr, "cruise_control") == 0) {
        p->templateKind = CfgPageTemplate::CRUISE_CONTROL;
        return;
    }
    if (strcmp(templateStr, "custom") != 0) {
        LOG_WARN("CFG", "page '%s': unknown template='%s' — falling back to 'custom'", p->id,
                 templateStr);
    }
    p->templateKind = CfgPageTemplate::CUSTOM;
}

DashboardOutcome allocPageWidgets(size_t totalWidgets, CfgPage *p, size_t *totalAllocBytes) {
    if (totalWidgets > kConfigMaxWidgetsPerPage) {
        LOG_WARN("CFG",
                 "page '%s': %u widgets exceed CONFIG_MAX_WIDGETS_PER_PAGE=%u — extras ignored",
                 p->id, static_cast<unsigned>(totalWidgets), kConfigMaxWidgetsPerPage);
    }
    const size_t cap =
        totalWidgets > kConfigMaxWidgetsPerPage ? kConfigMaxWidgetsPerPage : totalWidgets;
    if (cap == 0)
        return DashboardOutcome::Loaded;
    const size_t allocBytes = cap * sizeof(CfgWidget);
    if (*totalAllocBytes + allocBytes > kConfigDashboardHeapBudgetBytes) {
        LOG_ERROR("CFG",
                  "dashboard.json: cumulative widget alloc would exceed budget %u B "
                  "(at page '%s', running=%u + adding=%u) — refusing payload",
                  static_cast<unsigned>(kConfigDashboardHeapBudgetBytes), p->id,
                  static_cast<unsigned>(*totalAllocBytes), static_cast<unsigned>(allocBytes));
        ErrorStore::push(ERROR_SRC_CONFIG, "BUDGET", "payload_too_large");
        return DashboardOutcome::Refused;
    }
    p->widgets = new (std::nothrow) CfgWidget[cap]{};
    if (!p->widgets) {
        LOG_ERROR("CFG", "page '%s': widgets alloc (%u × %u B) failed", p->id,
                  static_cast<unsigned>(cap), static_cast<unsigned>(sizeof(CfgWidget)));
        ErrorStore::push(ERROR_SRC_CONFIG, "OOM", "widgets alloc failed");
        return DashboardOutcome::NoMemory;
    }
    p->widgetCapacity = static_cast<uint8_t>(cap);
    *totalAllocBytes += allocBytes;
    return DashboardOutcome::Loaded;
}

DashboardOutcome parsePages(JsonArrayConst pages, CfgDashboard *out) {
    out->pageCount = 0;
    if (pages.size() > kConfigMaxPages) {
        LOG_WARN("CFG", "dashboard.json: %u pages exceed CONFIG_MAX_PAGES=%u — extras ignored",
                 static_cast<unsigned>(pages.size()), kConfigMaxPages);
    }
    size_t totalAllocBytes = 0;
    for (JsonObjectConst page : pages) {
        if (out->pageCount >= kConfigMaxPages)
            break;
        CfgPage &p = out->pages[out->pageCount++];
        parsePageMeta(page, &p);
        p.widgetCount = 0;
        p.widgetCapacity = 0;
        p.widgets = nullptr;
        JsonArrayConst widgets = page["widgets"];
        const DashboardOutcome alloc = allocPageWidgets(widgets.size(), &p, &totalAllocBytes);
        if (alloc != DashboardOutcome::Loaded)
            return alloc;
        for (JsonObjectConst w : widgets) {
            if (p.widgetCount >= p.widgetCapacity)
                break;
            parseWidget(w, &p.widgets[p.widgetCount++]);
        }
    }
    return DashboardOutcome::Loaded;
}

DashboardOutcome buildDashboard(const JsonDocument &doc, CfgDashboard *out) {
    parseDashboardHeader(doc, out);
    parseTopBar(doc["topBar"], &out->topBar);
    parseThemes(doc, out);
    const DashboardOutcome pages = parsePages(doc["pages"], out);
    if (pages != DashboardOutcome::Loaded)
        return pages;
    return resolvePageLayouts(out) ? DashboardOutcome::Loaded : DashboardOutcome::Refused;
}

}

}
