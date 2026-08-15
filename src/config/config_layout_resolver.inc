#include "config_loader_internal.h"

#include "board_config.h"
#include "diag/error_store.h"
#include "diag/logger.h"
#include "layout_grid_rs.h"
#include "ui/dash_metrics.h"
#include "ui/screen_profile.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace ConfigLoaderInternal {

namespace {

bool declaresShiftStrip(const CfgPage &p) {
    for (uint8_t wi = 0; wi < p.widgetCount; ++wi) {
        if (p.widgets[wi].type == WidgetType::SHIFT_LIGHT)
            return true;
    }
    return false;
}

void resolveShiftStrip(CfgLayout &L, uint16_t designWidth) {
    LayoutGridRectRs rect = {};
    layout_grid_resolve_rs(L.col, L.colSpan, 0, 1, designWidth, designWidth, &rect);
    L.x = rect.x;
    L.w = rect.w;
    L.y = DashMetrics::kFramePaddingPx;
    L.h = DashMetrics::kShiftStripHeightPx;
}

struct PageBand {
    uint16_t designWidth;
    uint16_t designHeight;
    uint16_t reserved;
    uint16_t contentHeight;
    bool hasStrip;
};

PageBand pageBandFor(const CfgPage &p, const CfgDashboard &dash,
                     const ScreenProfile::DesignDimensions &design) {
    const uint16_t topBarHeight = p.showTopBar ? dash.topBar.height : 0;
    const bool hasStrip = declaresShiftStrip(p);
    const uint16_t stripBand = hasStrip ? DashMetrics::kShiftStripBandPx : 0;
    const uint16_t reserved = static_cast<uint16_t>(topBarHeight + stripBand);
    const uint16_t contentHeight = design.height > reserved ? design.height - reserved : 0;
    return {design.width, design.height, reserved, contentHeight, hasStrip};
}

bool isStrip(const CfgWidget &w, const PageBand &band) {
    return band.hasStrip && w.type == WidgetType::SHIFT_LIGHT;
}

void resolveWidgetLayout(CfgWidget &w, const PageBand &band) {
    CfgLayout &L = w.layout;
    if (isStrip(w, band)) {
        resolveShiftStrip(L, band.designWidth);
        return;
    }
    const uint8_t row = band.hasStrip && L.row > 0 ? static_cast<uint8_t>(L.row - 1) : L.row;
    LayoutGridRectRs rect = {};
    layout_grid_resolve_rs(L.col, L.colSpan, row, L.rowSpan, band.designWidth, band.contentHeight,
                           &rect);
    L.x = rect.x;
    L.y = rect.y;
    L.w = rect.w;
    L.h = rect.h;
}

int16_t screenY(const CfgWidget &w, const PageBand &band) {
    const uint16_t offset =
        isStrip(w, band) ? band.reserved - DashMetrics::kShiftStripBandPx : band.reserved;
    return static_cast<int16_t>(w.layout.y + offset);
}

void rejectOverflow(const CfgWidget &w, uint8_t pageNumber, int16_t y, uint16_t maxY) {
    s_rejection = {};
    s_rejection.present = true;
    s_rejection.pageNumber = pageNumber;
    strlcpy(s_rejection.widgetId, w.id, sizeof(s_rejection.widgetId));
    s_rejection.widgetY = y;
    s_rejection.widgetH = w.layout.h;
    s_rejection.maxY = static_cast<int16_t>(maxY);
    LOG_ERROR("CFG", "page %u: widget '%s' at y=%d h=%d exceeds %u px — config rejected",
              static_cast<unsigned>(pageNumber), w.id, y, w.layout.h, static_cast<unsigned>(maxY));
    char detail[52];
    snprintf(detail, sizeof(detail), "page %u widget %.16s y=%d h=%d",
             static_cast<unsigned>(pageNumber), w.id, y, w.layout.h);
    ErrorStore::push(ERROR_SRC_CONFIG, "OVERFLOW", detail);
}

bool resolvePageWidgets(const CfgPage &p, const PageBand &band, uint8_t pageNumber) {
    for (uint8_t wi = 0; wi < p.widgetCount; ++wi) {
        CfgWidget &w = p.widgets[wi];
        resolveWidgetLayout(w, band);
        const int16_t y = screenY(w, band);
        if (y + w.layout.h <= static_cast<int16_t>(band.designHeight))
            continue;
        rejectOverflow(w, pageNumber, y, band.designHeight);
        return false;
    }
    return true;
}

} // namespace

bool resolvePageLayouts(CfgDashboard *out) {
    const ScreenProfile::DesignDimensions design =
        ScreenProfile::lookupDesignDimensions(out->targetProfile);
    for (uint8_t pi = 0; pi < out->pageCount; ++pi) {
        const CfgPage &p = out->pages[pi];
        if (!resolvePageWidgets(p, pageBandFor(p, *out, design), static_cast<uint8_t>(pi + 1)))
            return false;
    }
    return true;
}

} // namespace ConfigLoaderInternal
