#pragma once
// input_buttons.h — Physical GPIO button → dashboard action dispatch (#833).
//
// Owns a small FreeRTOS task that polls every configured input pin at 1 kHz,
// debounces it in software, classifies presses as SHORT / LONG / DOUBLE, and
// hands the resulting event to `ActionDispatcher::dispatchAction`. The same
// action plumbing that LVGL touch buttons feed.

namespace InputButtons {

/**
 * Initialise pin modes for every loaded input binding and spawn the polling
 * task. Must be called AFTER `ConfigLoader::loadAll()` and AFTER the LVGL
 * mutex is created — the dispatcher path uses lv_async_call for page nav.
 * Safe to call once; subsequent calls are no-ops.
 */
void init();

} // namespace InputButtons
