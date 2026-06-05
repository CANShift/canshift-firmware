#pragma once
// usb_comm_internal.h — Cross-module forward decls for the USB CDC layer.
// Carved out of usb_comm.cpp during the #1207 refactor. Three translation
// units share this header:
//
//   - usb_comm.cpp        — orchestrator + transport: reserveRxBuf, init,
//                           tick, sink fan-out (sendLine / setAuxSink /
//                           hasAuxSink), handleLine, telemetry emit, CAN scan
//                           queue drain, host-activity tracking.
//   - usb_dispatch.cpp    — command parsing + dispatch: handleCommand and
//                           every command handler that does not touch the
//                           typed-config wire shape (PUT_CONFIG, PUT_FILE,
//                           SCREEN_SETTINGS, GET_CONFIG, CAN scan start/stop,
//                           GET_STATUS, day/night + calibration queueing).
//   - usb_config_sync.cpp — typed device.json / input_bindings.json GET/PUT
//                           handlers, the pre-allocated response buffer and
//                           its mutex, persistTypedConfigAndReboot.
//
// The PUBLIC API in usb_comm.h is byte-identical to pre-refactor. This header
// is internal — never include it from outside src/hal/usb/.

#include "usb_comm.h"

#include <ArduinoJson.h>
#include <stddef.h>
#include <stdint.h>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace UsbCommInternal {

// ---------------------------------------------------------------------------
// Shared with all three TUs — RX buffer reused by handlePutFile for in-place
// base64 decoding. Owned and lifecycle-managed by usb_comm.cpp.
// ---------------------------------------------------------------------------

extern char *s_rxBuf;

// ---------------------------------------------------------------------------
// Host-activity tracking — updated by every received command in
// usb_dispatch.cpp and by every received byte in usb_comm.cpp::tick(), read
// by UsbComm::isHostActive(). Single writer per source, single reader,
// 32-bit aligned on ESP32 so volatile is sufficient.
// ---------------------------------------------------------------------------

extern volatile uint32_t s_lastHostCmdMs;

// ---------------------------------------------------------------------------
// Dispatch entry point — called by both transport paths (USB tick() reading
// from Serial, and UsbComm::handleLine() routing from WiFi TCP/WS).
// Implemented in usb_dispatch.cpp.
// ---------------------------------------------------------------------------

void handleCommand(const char *jsonLine);

// ---------------------------------------------------------------------------
// Typed-config handlers — implemented in usb_config_sync.cpp. Exposed here
// so usb_dispatch.cpp can route the CMD_GET_/CMD_PUT_ cases to them.
//
// CMD_GET_DEVICE_CONFIG / CMD_GET_INPUT_BINDINGS routes are served by
// sendTypedConfigGet; CMD_PUT_DEVICE_CONFIG / CMD_PUT_INPUT_BINDINGS by the
// per-command shim handlers below.
// ---------------------------------------------------------------------------

void sendTypedConfigGet(const char *path, const char *fieldKey, const char *unwrapKey);
void handlePutDeviceConfig(const JsonObjectConst &obj);
void handlePutInputBindings(const JsonObjectConst &obj);

// Length cap for typed PUT payloads. Mirrors the worst-case input_bindings
// envelope (16 entries x ~256 B + wrapper) with comfortable headroom; the
// device_config payload is ~80 B so the same cap covers both. Anything
// above this is rejected before parse so a malicious / malformed host
// cannot exhaust the JsonDocument heap pool. Kept well under
// USB_RX_BUF_SIZE (CONFIG_JSON_DOC_DASHBOARD + 256 = 16 640 B) which is
// the upstream cap enforced by the RX line accumulator in tick().
constexpr size_t kTypedPutMaxPayloadBytes = 8192;

// ---------------------------------------------------------------------------
// Response-buffer lifecycle — the buffer + mutex live in usb_config_sync.cpp
// so the storage stays paired with its only consumer. init() in
// usb_comm.cpp lazily creates the mutex through this hook.
// ---------------------------------------------------------------------------

void initResponseBufferMutex();

// ---------------------------------------------------------------------------
// CAN scan queue accessors — the queue handle + portMUX + drop counter live
// in usb_dispatch.cpp because their lifecycle is owned by the
// CMD_CAN_SCAN_START / CMD_CAN_SCAN_STOP handlers. The transport layer
// (usb_comm.cpp) reaches in through these thin accessors for the CAN-task
// producer path (pushCanFrame) and the USB-task drain path
// (drainCanScanQueue).
// ---------------------------------------------------------------------------

bool canScanModeActive();
bool canScanQueueTrySend(const UsbComm::CanScanFrame &frame);
bool canScanQueueTryReceive(UsbComm::CanScanFrame &out);

// ---------------------------------------------------------------------------
// Tick-time housekeeping — abort any chunked CMD_PUT_FILE transfer that has
// stalled past CHUNK_TIMEOUT_MS. Driven by usb_comm.cpp::tick() once per
// tick so the chunk-transfer state stays owned by the dispatch module.
// ---------------------------------------------------------------------------

void tickChunkTransferTimeout();

// ---------------------------------------------------------------------------
// BurnOverlay observer indirection (#1207 #1314) — usb_dispatch.cpp calls
// these instead of #include "ui/burn_overlay.h" directly. The transport TU
// (usb_comm.cpp) owns the callback slots registered through
// UsbComm::setBurnOverlayShowCallback / setBurnOverlayShowErrorCallback.
// Both invokes are no-ops when no callback has been registered yet (boot
// race / sim build) — the storage write still proceeds, only the visual
// feedback is skipped.
// ---------------------------------------------------------------------------

void invokeBurnOverlayShow();
void invokeBurnOverlayShowError(int reason);

} // namespace UsbCommInternal
