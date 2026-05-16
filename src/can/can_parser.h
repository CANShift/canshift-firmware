#pragma once
// can_parser.h — CAN frame parser
//
// Data-driven: signal definitions are loaded from signals.json at runtime via
// `loadSignalDefinitions()`. Frame IDs and byte layouts come exclusively from
// that file — there is no hardcoded ECU-specific fallback. If signals.json
// fails to load, frames pass through unparsed (better than guessing at random
// bytes with assumed semantics — see #682).

#include <stdint.h>
#include <stddef.h>

namespace CanParser {

/**
 * Attempt to parse a CAN frame.
 * If the frame ID is recognized in the runtime signal table, decoded signals
 * are written to SignalStore. Frames with no matching definition are silently
 * dropped.
 *
 * @param frameId   29-bit or 11-bit CAN frame ID
 * @param data      Pointer to up to 8 data bytes
 * @param length    Number of data bytes (DLC)
 */
void parseFrame(uint32_t frameId, const uint8_t *data, uint8_t length);

/**
 * Load signal definitions from a runtime-parsed signals.json.
 * Called by ConfigLoader after loading signals.json. Without this call, the
 * parser is a no-op for every frame.
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

} // namespace CanParser
