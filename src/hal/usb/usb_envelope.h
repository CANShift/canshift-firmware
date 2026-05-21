#pragma once
// usb_envelope.h — Allocation-free JSON envelope helpers for the USB
// PUT_CONFIG path. Extracted from `usb_comm.cpp` (issue #912) so the host
// unit-test environment can link against the parsing logic without pulling
// in the rest of the USB stack (ArduinoJson, NimBLE, LVGL, storage driver).
//
// Both functions are stateless and side-effect free — safe to call from any
// task and trivially testable on the host.

#include <stddef.h>

namespace UsbEnvelope {

// Length-bounded substring search. Used in place of strstr() so an embedded
// NUL in the JSON line (corrupted USB stream, malformed input) cannot
// short-circuit the search before reaching the needle. Issue #884.
//
// Returns nullptr when `needleLen` is 0, when the haystack is shorter than
// the needle, or when no match exists.
const char *findNeedle(const char *haystack, size_t haystackLen, const char *needle,
                       size_t needleLen);

// Locate the value substring of the `"payload"` key inside a top-level JSON
// object. Returns a pointer to the first byte of the value (an opening '{')
// and writes the value length (including the closing '}') into *outLen.
// Returns nullptr when the envelope is malformed, the key is missing, or
// the value is not a JSON object.
//
// Allocation-free — walks the input with brace-balance + string-aware state.
// Honours escaped bytes inside JSON strings so `{` / `}` inside string
// values do not affect the depth counter.
const char *findPayloadSlice(const char *jsonLine, size_t lineLen, size_t *outLen);

} // namespace UsbEnvelope
