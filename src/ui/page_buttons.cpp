#include "ui/page_buttons.h"

#include "can/signal_map.h"
#include "config/config_loader.h"
#include "control_state_rs.h"
#include "diag/error_store.h"
#include "diag/logger.h"
#include "ui/page_turn_gate.h"

namespace {

struct Binding {
    const char *role;
    lv_dir_t direction;
    SignalId signal;
    PageButtonEdgeRs edge;
};

Binding s_bindings[] = {
    {"next", LV_DIR_LEFT, SignalIds::kSignalCount, {}},
    {"previous", LV_DIR_RIGHT, SignalIds::kSignalCount, {}},
};

SignalId resolve(const char *role, const char *signalName) {
    if (signalName[0] == '\0')
        return SignalIds::kSignalCount;
    const SignalId sid = signalIdFromName(signalName);
    if (sid < SignalIds::kSignalCount) {
        LOG_INFO("UI", "page button '%s' bound to signal '%s'", role, signalName);
        return sid;
    }
    LOG_ERROR("UI", "page button '%s': signal '%s' is not one the dash reads — ignored", role,
              signalName);
    ErrorStore::push(ERROR_SRC_CONFIG, "PAGE_BTN", "page button signal not readable");
    return SignalIds::kSignalCount;
}

bool pressed(Binding &binding, const SignalStore::SignalValue *snap, uint32_t nowMs) {
    if (binding.signal >= SignalIds::kSignalCount)
        return false;
    const SignalStore::SignalValue &reading = snap[binding.signal];
    return control_page_button_sample_rs(&binding.edge, reading.valid, reading.raw, nowMs);
}

} // namespace

void PageButtons::init() {
    const CfgPageButtons &cfg = ConfigLoader::getDashboardConfig().pageButtons;
    const char *signalNames[] = {cfg.next, cfg.previous};
    static_assert(sizeof(signalNames) / sizeof(signalNames[0]) ==
                      sizeof(s_bindings) / sizeof(s_bindings[0]),
                  "one configured signal per page button");
    for (size_t i = 0; i < sizeof(s_bindings) / sizeof(s_bindings[0]); ++i) {
        s_bindings[i].signal = resolve(s_bindings[i].role, signalNames[i]);
        s_bindings[i].edge = {};
    }
}

lv_dir_t PageButtons::tick(const SignalStore::SignalValue *snap, uint32_t nowMs) {
    lv_dir_t turn = LV_DIR_NONE;
    for (Binding &binding : s_bindings) {
        if (pressed(binding, snap, nowMs) && turn == LV_DIR_NONE)
            turn = binding.direction;
    }
    if (turn == LV_DIR_NONE || !PageTurnGate::open())
        return LV_DIR_NONE;
    LOG_VDEBUG("UI", "page button turns the page (dir=%d)", static_cast<int>(turn));
    return turn;
}
