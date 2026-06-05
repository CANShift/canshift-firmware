#pragma once
// ble_server_internal.h — cross-TU declarations for the ble_server split.
//
// The public BleServer:: API in `ble_server.h` is byte-identical to pre-split;
// this header is implementation-private and exposes the file-static state and
// helpers that the status + telemetry modules need from the orchestrator.
//
// Ownership: every extern declared here is *defined* in `ble_server.cpp`
// (the orchestrator). Helper TUs (`ble_status.cpp`, `ble_telemetry.cpp`) reach
// in via these decls — they never own the lifetime of these symbols.

#include "app_config.h"

#if APP_BLE_ENABLED

    #include <NimBLEDevice.h>
    #include <stddef.h>

namespace BleServerInternal {

// ---------------------------------------------------------------------------
// Module-static state (defined in ble_server.cpp)
// ---------------------------------------------------------------------------

// Characteristic pointers — owned by the GATT tree built in `setupGatt()`.
// Helper TUs MUST snapshot to a local before dereferencing (issue #1283):
//   auto *pStatus = BleServerInternal::s_pStatus;
//   if (!pStatus) return;
// `BleServer::stop()` can null these on a different task between the entry
// check and the setValue/notify call. The GATT-preserved path keeps the
// underlying characteristic object alive for the lifetime of the process,
// so the snapshot remains valid even if the global is cleared mid-call.
extern NimBLECharacteristic *s_pTele;
extern NimBLECharacteristic *s_pStatus;

extern bool s_connected;

// ---------------------------------------------------------------------------
// Cross-TU helpers
// ---------------------------------------------------------------------------

// Defined in `ble_status.cpp`. Rebuilds the STATUS JSON payload and pushes it
// into the STATUS characteristic value via `setValue()`. Does NOT notify —
// callers decide whether to notify subscribers afterwards.
void updateStatus();

// Defined in `ble_telemetry.cpp`. Snapshots subscriber state, builds the
// telemetry frame on the stack, and notifies subscribers. Also drives the
// 2s STATUS refresh divider. Called from `BleServer::tick()`.
void emitTelemetry();

} // namespace BleServerInternal

#endif // APP_BLE_ENABLED
