// config_file_loaders.cpp — Per-file (`dashboard` / `signals` / `device` /
// `input_bindings`) loaders driven by ConfigLoader::loadAll() (#1207).
//
// Extracted from `config_loader.cpp`. Each `loadX()` here pulls its document
// through the shared `readAndParseWithBak` helper (in `json_helpers.cpp`),
// validates schema-level invariants via `config_validators.cpp`, hands per-
// widget / per-signal nodes off to the parsers in `config_parser.cpp`, and
// writes into the file-static config storage exposed by
// `config_loader_internal.h`. Rollback snapshots are taken/restored against
// the orchestrator-owned `acquireRollbackSnapshot()` buffer.

#include "config_loader_internal.h"

#include "app_config.h"
#include "board_config.h"
#include "config/parse_utils.h"
#include "diag/error_store.h"
#include "diag/logger.h"
#include "hal/storage/storage_driver.h"

#include <ArduinoJson.h>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <new>

namespace ConfigLoaderInternal {

bool loadDashboard() {
    auto *out = new (std::nothrow) CfgDashboard{};
    if (!out) {
        LOG_ERROR("CFG", "loadDashboard: staging alloc (%u B) failed",
                  static_cast<unsigned>(sizeof(CfgDashboard)));
        ErrorStore::push(ERROR_SRC_CONFIG, "OOM", "dashboard staging alloc failed");
        return false;
    }

    JsonDocument doc;
    if (!readAndParseWithBak(CONFIG_PATH_DASHBOARD, doc)) {
        LOG_ERROR("CFG", "dashboard.json unreadable (primary + .bak)");
        ErrorStore::push(ERROR_SRC_CONFIG, "READ_FAIL", "dashboard.json unreadable");
        delete out;
        return false;
    }

    strlcpy(out->version, doc["version"] | "", sizeof(out->version));
    checkSchemaVersion("dashboard.json", out->version);
    strlcpy(out->name, doc["name"] | "", sizeof(out->name));
    strlcpy(out->defaultPageId, doc["defaultPageId"] | "", sizeof(out->defaultPageId));
    out->revLimitRpm = doc["revLimitRpm"] | 7200.0f;
    // Target screen profile (issues #17, #18). Optional in JSON — pre-#1128
    // dashboards omit it; resolveScreenProfile() in canshift-core treats
    // missing as DEFAULT_SCREEN_PROFILE_ID. Mirror that here so the
    // downstream scale computation (ui/screen_profile.cpp) reads a stable
    // non-empty id and never has to special-case the empty string.
    strlcpy(out->targetProfile, doc["targetProfile"] | "crowpanel-28", sizeof(out->targetProfile));
    JsonObjectConst topBar = doc["topBar"];
    out->topBar.height = topBar["height"] | 30;
    parseColor(topBar["bgColor"] | "#111111", &out->topBar.bgColor);
    parseColor(topBar["textColor"] | "#AAAAAA", &out->topBar.textColor);

    JsonArrayConst topBarLayout = topBar["layout"];
    out->topBar.itemCount = 0;
    if (!topBarLayout.isNull()) {
        const size_t total = topBarLayout.size();
        if (total > CFG_MAX_TOPBAR_ITEMS) {
            LOG_WARN("CFG",
                     "topBar.layout: %u items exceed CFG_MAX_TOPBAR_ITEMS=%u — extras ignored",
                     static_cast<unsigned>(total), CFG_MAX_TOPBAR_ITEMS);
        }
        for (JsonObjectConst item : topBarLayout) {
            if (out->topBar.itemCount >= CFG_MAX_TOPBAR_ITEMS)
                break;
            CfgTopBarItem tmp{};
            tmp.kind = parseTopBarItemKind(item["type"] | "");
            tmp.position = parseTopBarItemPos(item["position"] | "left");
            strlcpy(tmp.signalId, item["signal"] | "", CFG_MAX_SIGNAL_LEN);
            strlcpy(tmp.text, item["text"] | "", sizeof(tmp.text));
            strlcpy(tmp.format, item["format"] | "", sizeof(tmp.format));
            if (tmp.kind == TopBarItemKind::UNKNOWN) {
                LOG_WARN("CFG", "topBar.layout[%u]: unknown type — item dropped",
                         static_cast<unsigned>(out->topBar.itemCount));
                continue; // slot untouched — no stale data risk
            }
            out->topBar.items[out->topBar.itemCount++] = tmp;
        }
    } else {
        // No layout in config — mirror canshift-core DEFAULT_TOP_BAR_LAYOUT
        LOG_INFO("CFG", "topBar.layout missing — applying default layout");
        uint8_t &n = out->topBar.itemCount;
        auto &items = out->topBar.items;
        auto addItem = [&](TopBarItemKind kind, TopBarItemPos pos, const char *sig,
                           const char *txt) {
            // `n` is the ref-captured out->topBar.itemCount, set to 0
            // above. clang-analyzer's path tracking loses the initialisation
            // across the lambda capture and reports a false-positive
            // uninitialised-deref on the read below.
            // NOLINTNEXTLINE(clang-analyzer-core.NullDereference)
            if (n >= CFG_MAX_TOPBAR_ITEMS)
                return;
            CfgTopBarItem &out = items[n++];
            out.kind = kind;
            out.position = pos;
            strlcpy(out.signalId, sig, sizeof(out.signalId));
            strlcpy(out.text, txt, sizeof(out.text));
            out.format[0] = '\0';
        };
        addItem(TopBarItemKind::STATUS_DOT, TopBarItemPos::LEFT, "any", "");
        addItem(TopBarItemKind::LABEL, TopBarItemPos::LEFT, "", "CAN");
        addItem(TopBarItemKind::BLE_ICON, TopBarItemPos::RIGHT, "", "");
        addItem(TopBarItemKind::USB_ICON, TopBarItemPos::RIGHT, "", "");
        addItem(TopBarItemKind::SEPARATOR, TopBarItemPos::RIGHT, "", "");
        addItem(TopBarItemKind::THEME_TOGGLE, TopBarItemPos::RIGHT, "", "");
    }

    // Optional day theme (hasDayTheme = false when key absent)
    JsonObjectConst dayThemeJson = doc["dayTheme"];
    out->hasDayTheme = !dayThemeJson.isNull();
    if (out->hasDayTheme) {
        parseColor(dayThemeJson["bgColor"] | "#DDDDDD", &out->dayTheme.bgColor);
    }

    JsonArrayConst pages = doc["pages"];
    out->pageCount = 0;
    const size_t totalPages = pages.size();
    if (totalPages > CONFIG_MAX_PAGES) {
        LOG_WARN("CFG", "dashboard.json: %u pages exceed CONFIG_MAX_PAGES=%u — extras ignored",
                 static_cast<unsigned>(totalPages), CONFIG_MAX_PAGES);
    }

    for (JsonObjectConst page : pages) {
        if (out->pageCount >= CONFIG_MAX_PAGES)
            break;

        CfgPage &p = out->pages[out->pageCount++];
        strlcpy(p.id, page["id"] | "", CFG_MAX_ID_LEN);
        strlcpy(p.bgImagePath, page["backgroundImage"] | "", CFG_MAX_PATH_LEN);
        parseColor(page["backgroundColor"] | "#1A1A1A", &p.bgColor);

        p.showTopBar = page["showTopBar"] | true;
        p.visible = page["visible"] | true;
        // Page template (issue #451). Absent / "custom" / unknown → CUSTOM, so
        // every pre-#451 config keeps rendering its widgets[] grid unchanged.
        const char *templateStr = page["template"] | "custom";
        if (strcmp(templateStr, "cruise_control") == 0) {
            p.templateKind = CfgPageTemplate::CRUISE_CONTROL;
        } else {
            if (strcmp(templateStr, "custom") != 0) {
                LOG_WARN("CFG", "page '%s': unknown template='%s' — falling back to 'custom'", p.id,
                         templateStr);
            }
            p.templateKind = CfgPageTemplate::CUSTOM;
        }
        p.widgetCount = 0;
        p.widgetCapacity = 0;
        p.widgets = nullptr;

        JsonArrayConst widgets = page["widgets"];
        const size_t totalWidgets = widgets.size();
        if (totalWidgets > CONFIG_MAX_WIDGETS_PER_PAGE) {
            LOG_WARN("CFG",
                     "page '%s': %u widgets exceed CONFIG_MAX_WIDGETS_PER_PAGE=%u — extras ignored",
                     p.id, static_cast<unsigned>(totalWidgets), CONFIG_MAX_WIDGETS_PER_PAGE);
        }
        const size_t cap =
            totalWidgets > CONFIG_MAX_WIDGETS_PER_PAGE ? CONFIG_MAX_WIDGETS_PER_PAGE : totalWidgets;
        if (cap > 0) {
            p.widgets = new (std::nothrow) CfgWidget[cap]{};
            if (!p.widgets) {
                LOG_ERROR("CFG", "page '%s': widgets alloc (%u × %u B) failed", p.id,
                          static_cast<unsigned>(cap), static_cast<unsigned>(sizeof(CfgWidget)));
                ErrorStore::push(ERROR_SRC_CONFIG, "OOM", "widgets alloc failed");
                delete out;
                return false;
            }
            p.widgetCapacity = static_cast<uint8_t>(cap);
        }
        for (JsonObjectConst w : widgets) {
            if (p.widgetCount >= p.widgetCapacity)
                break;
            parseWidget(w, &p.widgets[p.widgetCount++]);
        }
    }

    out->loaded = true;
    LOG_INFO("CFG", "dashboard.json loaded: %d pages", out->pageCount);
    CfgDashboard *prev = s_dashboard;
    s_dashboard = out;
    delete prev;
    return true;
}

bool loadSignals() {
    // Snapshot prior state into the rollback buffer for rollback on parse
    // failure (issue #458, audit F-HI-3 / umbrella #1014) — same pattern as
    // loadDashboard(). Boot-time config load is serial and loadDashboard has
    // already returned, so the shared rollback buffer is free to reuse here.
    // See loadDashboard() for the null-snapshot rationale (issue #1073).
    uint8_t *snapshot = acquireRollbackSnapshot();
    if (snapshot) {
        memcpy(snapshot, &s_signals, sizeof(CfgSignalConfig));
    }

    JsonDocument doc; // ArduinoJson v7 — dynamic
    if (!readAndParseWithBak(CONFIG_PATH_SIGNALS, doc)) {
        LOG_ERROR("CFG", "signals.json unreadable (primary + .bak)");
        ErrorStore::push(ERROR_SRC_CONFIG, "READ_FAIL", "signals.json unreadable");
        if (snapshot) {
            memcpy(&s_signals, snapshot, sizeof(CfgSignalConfig));
        }
        return false;
    }

    strlcpy(s_signals.version, doc["version"] | "", sizeof(s_signals.version));
    checkSchemaVersion("signals.json", s_signals.version);
    strlcpy(s_signals.protocol, doc["protocol"] | "", sizeof(s_signals.protocol));
    s_signals.canSpeedKbps = doc["canSpeedKbps"] | 500;
    s_signals.signalCount = 0;

    // Outbound frame overrides — issue #317. signals.json `out` block lets the
    // user override baked frame IDs (currently just map_switch). Missing keys
    // leave the field at zero so the dispatcher falls back to the compiled-in
    // default. `extended` is auto-set when the ID exceeds the 11-bit range.
    s_signals.out.mapSwitchFrameId = 0;
    s_signals.out.mapSwitchExtended = false;
    JsonObjectConst outObj = doc["out"];
    if (!outObj.isNull()) {
        JsonObjectConst mapSwitch = outObj["map_switch"];
        if (!mapSwitch.isNull()) {
            JsonVariantConst idv = mapSwitch["id"];
            uint32_t id = 0;
            if (idv.is<uint32_t>()) {
                id = idv.as<uint32_t>();
            } else {
                const char *idStr = idv.as<const char *>();
                if (idStr && !parseU32Strict(idStr, 0, &id)) {
                    LOG_WARN("CFG",
                             "signals.json out.map_switch.id='%s' is not a valid number — ignored",
                             idStr);
                    id = 0;
                }
            }
            if (id > kCanExtendedIdMax) {
                LOG_WARN("CFG", "signals.json out.map_switch.id=0x%lX exceeds 29-bit max — ignored",
                         static_cast<unsigned long>(id));
                id = 0;
            }
            s_signals.out.mapSwitchFrameId = id;
            const bool extendedFlag = mapSwitch["extended"] | false;
            s_signals.out.mapSwitchExtended = extendedFlag || (id > kCanStandardIdMax);
        }
    }

    JsonArrayConst signals = doc["signals"];
    for (JsonObjectConst sig : signals) {
        if (s_signals.signalCount >= CONFIG_MAX_SIGNALS)
            break;

        CfgSignalDef &s = s_signals.signals[s_signals.signalCount++];
        {
            const char *rawName = sig["name"] | "";
            const size_t nameLen = strlcpy(s.name, rawName, CFG_MAX_SIGNAL_LEN);
            if (nameLen >= CFG_MAX_SIGNAL_LEN) {
                LOG_WARN(
                    "CFG",
                    "signals.json: dropping signal — name too long (%u chars, max %u): '%s...'",
                    static_cast<unsigned>(nameLen), static_cast<unsigned>(CFG_MAX_SIGNAL_LEN - 1),
                    s.name);
                --s_signals.signalCount;
                continue;
            }
        }
        {
            const char *raw = sig["canFrameId"] | (const char *)nullptr;
            if (!parseU32Strict(raw, 16, &s.canFrameId)) {
                LOG_WARN("CFG", "signals.json: dropping '%s' — invalid canFrameId '%s'", s.name,
                         raw ? raw : "(null)");
                --s_signals.signalCount;
                continue;
            }
        }
        s.startByte = sig["startByte"] | 0;
        s.byteLength = sig["byteLength"] | 1;
        s.bigEndian = sig["bigEndian"] | true;
        s.isSigned = sig["signed"] | false;
        s.scale = sig["scale"] | 1.0f;
        s.offset = sig["offset"] | 0.0f;
        {
            const char *rawUnit = sig["unit"] | "";
            const size_t unitLen = strlcpy(s.unit, rawUnit, sizeof(s.unit));
            if (unitLen >= sizeof(s.unit)) {
                LOG_WARN("CFG",
                         "signals.json: '%s' unit too long (%u chars, max %u) — unit reset to \"\"",
                         s.name, static_cast<unsigned>(unitLen),
                         static_cast<unsigned>(sizeof(s.unit) - 1));
                s.unit[0] = '\0';
            }
        }
        s.minValue = sig["min"] | 0.0f;
        s.maxValue = sig["max"] | 100.0f;
        {
            JsonVariantConst wv = sig["warningLevel"];
            s.warningLevel = wv.isNull() ? NAN : wv.as<float>();
            JsonVariantConst dv = sig["dangerLevel"];
            s.dangerLevel = dv.isNull() ? NAN : dv.as<float>();
            JsonVariantConst hwv = sig["highWarningLevel"];
            s.highWarningLevel = hwv.isNull() ? NAN : hwv.as<float>();
            JsonVariantConst hdv = sig["highDangerLevel"];
            s.highDangerLevel = hdv.isNull() ? NAN : hdv.as<float>();
        }
        s.timeoutMs = sig["timeoutMs"] | SIGNAL_DEFAULT_TIMEOUT_MS;
        {
            const char *bitMaskStr = sig["bitMask"] | (const char *)nullptr;
            if (bitMaskStr) {
                uint32_t maskVal = 0;
                if (parseU32Strict(bitMaskStr, 16, &maskVal)) {
                    s.bitMask = static_cast<uint8_t>(maskVal);
                } else {
                    LOG_WARN("CFG",
                             "signals.json: '%s' bitMask='%s' is not valid hex — using 0 (no mask)",
                             s.name, bitMaskStr);
                    s.bitMask = 0;
                }
            } else {
                s.bitMask = 0;
            }
        }

        // Per-signal color ramp (issue #430). Optional — when absent, the
        // widget renderer falls back to a default lookup keyed on the name.
        parseColorRamp(sig["colorRamp"], s.name, &s.colorRamp);

        // OBD-II polling block (issue #841 — phase 3 of #556). Optional —
        // when absent the signal stays in legacy passive-broadcast mode
        // (`pollIntervalMs == 0`). When present, Obd2Poller sends a request
        // frame at `pollIntervalMs` and decodes the response into this
        // signal. v1 supports Mode 01 only — out-of-range modes/intervals
        // are silently dropped (logged) so a malformed config keeps the
        // signal usable as a passive entry rather than rejecting the whole
        // catalog.
        s.pollMode = 0;
        s.pollPid = 0;
        s.pollIntervalMs = 0;
        JsonObjectConst pollObj = sig["polling"];
        if (!pollObj.isNull()) {
            const uint32_t mode = pollObj["mode"] | 0u;
            const uint32_t pid = pollObj["pid"] | 0u;
            const uint32_t intervalMs = pollObj["intervalMs"] | 0u;
            const bool modeOk = (mode == 0x01u);
            const bool pidOk = (pid <= 0xFFu);
            const bool intervalOk =
                (intervalMs >= OBD2_MIN_INTERVAL_MS_FW && intervalMs <= OBD2_MAX_INTERVAL_MS_FW);
            if (modeOk && pidOk && intervalOk) {
                s.pollMode = static_cast<uint8_t>(mode);
                s.pollPid = static_cast<uint8_t>(pid);
                s.pollIntervalMs = intervalMs;
            } else {
                LOG_WARN("CFG",
                         "signals.json: '%s' polling block invalid "
                         "(mode=0x%02X pid=0x%02X intervalMs=%u) — broadcast fallback",
                         s.name, static_cast<unsigned>(mode), static_cast<unsigned>(pid),
                         static_cast<unsigned>(intervalMs));
            }
        }

        // ----- Validate decoder-critical fields (issues #197 / #198) -----
        // CAN classic frames are 8 bytes; byteLength must be 1, 2, or 4 to
        // produce a well-defined sign-extend and a bounded read.
        // kCanFrameMaxBytes lives in app_config.h (F-LO-3).
        const bool byteLenValid = (s.byteLength == 1 || s.byteLength == 2 || s.byteLength == 4);
        const bool startInRange = (s.startByte < kCanFrameMaxBytes);
        const bool fitsInFrame =
            (static_cast<uint16_t>(s.startByte) + static_cast<uint16_t>(s.byteLength) <=
             kCanFrameMaxBytes);
        if (!byteLenValid || !startInRange || !fitsInFrame) {
            LOG_WARN("CFG",
                     "signals.json: dropping '%s' (startByte=%u byteLength=%u) — out of range",
                     s.name, s.startByte, s.byteLength);
            --s_signals.signalCount;
            continue;
        }
    }

    s_signals.loaded = true;
    LOG_INFO("CFG", "signals.json loaded: %d signals", s_signals.signalCount);
    return true;
}

bool loadDevice() {
    // device.json is small (CfgDeviceConfig is ~8 bytes); snapshot lives on
    // the stack. The rollback is defensive — today the only mutations live
    // at the tail of this function, but keeping the pattern uniform with
    // loadDashboard / loadSignals (issue #458) protects against future
    // refactors that move assignments earlier.
    const CfgDeviceConfig prev = s_device;

    if (!StorageDriver::fileExists(CONFIG_PATH_DEVICE)) {
        LOG_INFO("CFG", "device.json not found — using board_config.h defaults");
        s_device = prev;
        return false;
    }

    logLargestFreeBlock(CONFIG_PATH_DEVICE);

    JsonDocument doc;
    DeserializationError err = StorageDriver::parseJsonFile(CONFIG_PATH_DEVICE, doc);

    if (err) {
        LOG_WARN("CFG", "device.json parse error: %s — using defaults", err.c_str());
        s_device = prev;
        return false;
    }

    s_device.canSpeedKbps = doc["can_speed_kbps"] | 0;
    s_device.twaiTxPin = doc["twai_tx_pin"] | -1;
    s_device.twaiRxPin = doc["twai_rx_pin"] | -1;
    s_device.loaded = true;
    LOG_INFO("CFG", "device.json loaded: CAN=%ukbps TX=GPIO%d RX=GPIO%d", s_device.canSpeedKbps,
             s_device.twaiTxPin, s_device.twaiRxPin);
    return true;
}

bool loadInputBindings() {
    s_inputs.count = 0;
    s_inputs.loaded = false;
    memset(s_inputs.bindings, 0, sizeof(s_inputs.bindings));

    if (!StorageDriver::fileExists(CONFIG_PATH_INPUTS)) {
        LOG_INFO("CFG", "input_bindings.json not found — no physical buttons configured");
        return false;
    }

    JsonDocument doc;
    DeserializationError err = StorageDriver::parseJsonFile(CONFIG_PATH_INPUTS, doc);
    if (err) {
        LOG_WARN("CFG", "input_bindings.json parse error: %s — ignoring file", err.c_str());
        return false;
    }

    JsonArrayConst arr = doc["input_bindings"].as<JsonArrayConst>();
    if (arr.isNull()) {
        LOG_WARN("CFG", "input_bindings.json missing 'input_bindings' array — ignoring file");
        return false;
    }

    for (JsonObjectConst entry : arr) {
        if (s_inputs.count >= CFG_MAX_INPUT_BINDINGS) {
            LOG_WARN("CFG", "input_bindings.json: > %u entries — extras dropped",
                     static_cast<unsigned>(CFG_MAX_INPUT_BINDINGS));
            break;
        }
        CfgInputBinding &b = s_inputs.bindings[s_inputs.count];
        memset(&b, 0, sizeof(b));
        strlcpy(b.id, entry["id"] | "", sizeof(b.id));
        const int pin = entry["pin"] | -1;
        if (pin < 0 || pin > 39) {
            LOG_WARN("CFG", "input binding '%s': pin=%d out of range — dropped", b.id, pin);
            continue;
        }
        if (isPinConflict(static_cast<int8_t>(pin))) {
            LOG_WARN("CFG", "input binding '%s': pin=%d conflicts with TWAI — dropped", b.id, pin);
            continue;
        }
        b.pin = static_cast<int8_t>(pin);
        b.active = parseInputActive(entry["active"] | "low");
        b.pullup = entry["pullup"] | true;
        int debounce = entry["debounce_ms"] | 20;
        if (debounce < 1)
            debounce = 1;
        if (debounce > 500)
            debounce = 500;
        b.debounceMs = static_cast<uint16_t>(debounce);
        b.kind = parseInputPressKind(entry["kind"] | "short");
        JsonObjectConst action = entry["action"].as<JsonObjectConst>();
        if (action.isNull()) {
            LOG_WARN("CFG", "input binding '%s': missing 'action' — dropped", b.id);
            continue;
        }
        parseButtonAction(action, &b.action);
        if (b.action.type == CfgButtonActionType::UNKNOWN) {
            LOG_WARN("CFG", "input binding '%s': unknown / invalid action — dropped", b.id);
            continue;
        }
        strlcpy(b.signal, entry["signal"] | "", sizeof(b.signal));
        s_inputs.count++;
    }

    s_inputs.loaded = true;
    LOG_INFO("CFG", "input_bindings.json loaded: %u bindings", s_inputs.count);
    return true;
}

} // namespace ConfigLoaderInternal
