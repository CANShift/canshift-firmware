#pragma once
// maxxecu_parser.h — CAN frame decoder
//
// Data-driven parser: signal definitions are loaded from signals.json at runtime.
// The hardcoded defaults here serve as a fallback only.
//
// IMPORTANT: The frame IDs and byte positions below are EXAMPLES.
// You MUST verify them against your ECU's CAN output configuration before use.
// Use Studio's CAN Scanner to capture live frames and map them in signals.json.

#include <stdint.h>
#include <stddef.h>

namespace MaxxEcuParser {

// ---------------------------------------------------------------------------
// Example CAN frame IDs — verify against your ECU config
// ---------------------------------------------------------------------------

// Frame set 1 — Primary engine data
// Typical update rate: 10-20 ms (50-100 Hz)
static constexpr uint32_t FRAME_ID_ENGINE_1 = 0x370; // TODO: Verify

// Frame set 2 — Secondary engine data
static constexpr uint32_t FRAME_ID_ENGINE_2 = 0x371; // TODO: Verify

// Frame set 3 — Temperatures and pressures
static constexpr uint32_t FRAME_ID_TEMPS = 0x372; // TODO: Verify

// Frame set 4 — Electrical and misc
static constexpr uint32_t FRAME_ID_ELEC = 0x373; // TODO: Verify

// Frame set 5 — Status flags / bitmask
static constexpr uint32_t FRAME_ID_FLAGS = 0x374; // TODO: Verify

// Frame set 6 — Map number and profile info (if enabled in ECU)
static constexpr uint32_t FRAME_ID_MAP_INFO = 0x375; // TODO: Verify

// ---------------------------------------------------------------------------
// Signal byte layout for FRAME_ID_ENGINE_1 (8 bytes)
// These are ASSUMED positions — must be validated against your ECU's output config
//
// Byte 0-1: RPM (uint16, big-endian, 1 RPM/bit, 0-15000 RPM)
// Byte 2:   TPS (uint8, 0.5 %/bit, 0-100%)
// Byte 3-4: MAP (uint16, big-endian, 0.1 kPa/bit, 0-400 kPa)
// Byte 5:   IAT (uint8, 1 °C/bit, offset -40 → -40 to 215 °C)
// Byte 6-7: Speed (uint16, big-endian, 0.1 km/h per bit)
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Signal byte layout for FRAME_ID_ENGINE_2 (8 bytes)
// Byte 0-1: Lambda * 1000 (uint16, e.g. 1000 = lambda 1.00)
// Byte 2:   Gear (uint8, 0=neutral, 1-6)
// Byte 3-4: Fuel pressure (uint16, 0.01 bar/bit)
// Byte 5-6: (reserved / TBD)
// Byte 7:   (reserved)
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Signal byte layout for FRAME_ID_TEMPS (8 bytes)
// Byte 0-1: Coolant temperature (int16, big-endian, 0.1 °C/bit, offset -40)
// Byte 2-3: Oil temperature    (int16, big-endian, 0.1 °C/bit, offset -40)
// Byte 4-5: Oil pressure       (uint16, big-endian, 0.01 bar/bit)
// Byte 6-7: (reserved)
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Signal byte layout for FRAME_ID_ELEC (8 bytes)
// Byte 0-1: Battery voltage (uint16, big-endian, 0.01 V/bit)
// Byte 2-7: (reserved / future)
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Signal byte layout for FRAME_ID_FLAGS (1 byte bitmask)
// Bit 0: MIL (check engine)
// Bit 1: Launch control active
// Bit 2: Flat shift active
// Bit 3: Anti-lag active
// Bit 4: Traction control cut active
// Bit 5-7: (reserved)
// ---------------------------------------------------------------------------

/**
     * Attempt to parse a CAN frame.
     * If the frame ID is recognized, decoded signals are written to SignalStore.
     * Unknown frame IDs are silently ignored.
     *
     * @param frameId   29-bit or 11-bit CAN frame ID
     * @param data      Pointer to up to 8 data bytes
     * @param length    Number of data bytes (DLC)
     */
void parseFrame(uint32_t frameId, const uint8_t *data, uint8_t length);

/**
     * Load signal definitions from a runtime-parsed signals.json.
     * This overrides the hardcoded defaults above.
     * Called by ConfigLoader after loading signals.json.
     */
void loadSignalDefinitions();

namespace detail {
// Generic multi-byte decoder. Exposed for unit testing only — production
// callers should go through `parseFrame`.
//
// Decodes `byteLen` bytes starting at `startByte` into a float, applying:
//   - endianness (`bigEndian` selects byte order)
//   - signedness (`isSigned` triggers two's-complement sign-extension)
//   - bit mask (when `bitMask != 0`, returns 0.0 or 1.0 based on the mask)
//   - linear scaling (`raw * scale + offset`)
// Returns 0.0f for out-of-range start/length combinations.
float decodeBytes(const uint8_t *data, uint8_t startByte, uint8_t byteLen, bool bigEndian,
                  bool isSigned, uint8_t bitMask, float scale, float offset);
} // namespace detail

} // namespace MaxxEcuParser
