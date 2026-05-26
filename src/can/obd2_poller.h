#pragma once
// obd2_poller.h — OBD-II request/response poller (issue #841 — phase 3 of #556)
//
// CANShift's default decode path is passive: the firmware listens to the CAN
// bus and parses whatever frames the ECU broadcasts on its own. OBD-II ECUs
// do not broadcast — the dash must SEND a request frame (functional ID
// 0x7DF, mode 0x01, PID byte) and wait for a response (0x7E8). This module
// owns the request side of that loop and the response decoder.
//
// Scope (v1)
//   - Mode 01 PIDs only — Modes 02..09 land in a follow-up if needed.
//   - Single ECU at 0x7DF / 0x7E8. Multi-ECU (0x7E9..0x7EF) + ISO-TP
//     multi-frame are deferred.
//   - One signal = one PID = one polling slot. A signal with no `polling`
//     block stays on the broadcast path (CanParser handles those).
//
// Threading
//   `init()` runs once after `CanParser::loadSignalDefinitions()` populates
//   the runtime signal table — call it from the CAN task before entering
//   `tick()`. `tick()` and `onRxFrame()` are invoked exclusively from the
//   CAN task and so do not need their own locking; both are non-blocking.
//
// Wire format (response — Mode 01)
//   byte 0: length (typically 0x03 for a single-byte PID, 0x04 for 2 bytes)
//   byte 1: mode + 0x40 (e.g. 0x41 for Mode 01)
//   byte 2: PID byte (echo of the request)
//   bytes 3..6: payload bytes A B C D (signal-specific decode)
//
// Diagnostics
//   `pollsSent()` / `responsesMatched()` / `responsesMissed()` are exposed
//   so a future on-screen diag panel can surface bus health without sniffing
//   the poller's internal state.

#include <stdint.h>

#include "can/signal_map.h"

namespace Obd2Poller {

/**
 * Populate the polling table from the currently-loaded signal config. Reads
 * `ConfigLoader::getSignalConfig()` and copies one entry per signal with
 * `pollIntervalMs > 0` into the internal slot array. Safe to call multiple
 * times — each call resets the table from scratch.
 */
void init();

/**
 * Non-blocking poll-scheduler tick. Wired into `CanManager::tick()` on the
 * CAN task. For every slot whose `nextPollMs <= nowMs`, enqueue one OBD-II
 * request frame (TWAI hand-off is non-blocking) and advance `nextPollMs` by
 * the slot's interval. Slots that fail to send simply retry on the next tick.
 *
 * @param nowMs Current millis() — passed in so the caller can already have
 *              taken it for other bookkeeping in the same iteration without
 *              forcing two clock reads.
 */
void tick(uint32_t nowMs);

/**
 * Inspect a received CAN frame and, when it is an OBD-II response for one
 * of our pending PIDs, decode it into the matching SignalStore entry.
 *
 * @param frameId   CAN identifier of the inbound frame.
 * @param data      Up to 8 payload bytes.
 * @param length    Data Length Code (already clamped by the caller).
 * @return true when the frame was a recognised OBD-II response that this
 *         poller consumed — `CanParser::parseFrame()` must SKIP the frame
 *         in that case to avoid double-decoding under a misconfigured
 *         passive-broadcast entry on the same ID. false otherwise.
 */
bool onRxFrame(uint32_t frameId, const uint8_t *data, uint8_t length);

/** Total request frames the poller successfully handed to the TWAI driver. */
uint32_t pollsSent();

/** Total OBD-II responses successfully decoded into SignalStore. */
uint32_t responsesMatched();

/** Total polls where the next scheduled tick fired before the response
 *  arrived — informational only; equals `pollsSent() - responsesMatched()`
 *  rounded by in-flight frames. */
uint32_t responsesMissed();

/** Number of active polling slots (signals with `pollIntervalMs > 0`). */
uint8_t activeSlotCount();

} // namespace Obd2Poller
