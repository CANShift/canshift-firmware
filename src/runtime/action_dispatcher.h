#pragma once
// action_dispatcher.h — Single execution path for dashboard button actions.
//
// On-screen LVGL button widgets (`button_widget.cpp`) and physical GPIO
// button bindings (`input_buttons.cpp`) both route through `dispatchAction`
// so a NavigateAction / MapSwitchAction / CanRawAction / CruiseControlAction
// behaves identically regardless of the input source (issue #833).
//
// `isActive` is the toggle-state hint used by CAN_RAW actions to choose
// between the arm/disarm payload (`data` vs `dataOff`). Non-toggle inputs
// pass `true`.

#include "config/config_types.h"

namespace ActionDispatcher {

/**
 * Execute the action. Safe to call from the UI task or from an input task
 * (CAN/SignalStore calls inside are thread-safe).
 */
void dispatchAction(const CfgButtonAction &a, bool isActive);

} // namespace ActionDispatcher
