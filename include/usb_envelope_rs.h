// usb_envelope_rs.h — C ABI for the Rust usb-envelope crate (issue #1177 R-7).
//
// Hand-written. The FFI surface is two stateless functions over byte buffers;
// bindgen would drag libclang + a build.rs into the firmware build for no
// gain. Keep this header in sync with `rust/usb-envelope/src/ffi.rs` — both
// must move together in any PR that changes the bridge signature.
//
// `usb_envelope.cpp` consumes this header behind the existing C++
// `UsbEnvelope::findNeedle` / `findPayloadSlice` interface when built with
// `USE_RUST_USB_ENVELOPE=1` so callers (only `usb_comm.cpp`) don't change.

#ifndef CANSHIFT_USB_ENVELOPE_RS_H
#define CANSHIFT_USB_ENVELOPE_RS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Length-bounded substring search. Returns a pointer into `haystack` at the
// first occurrence of `needle`, or NULL. Returns NULL when `needle_len == 0`,
// `haystack_len < needle_len`, or either pointer is NULL. The Rust shim
// tightens the null-pointer contract relative to the C++ original (which
// dereferenced) — safe to feed defensive callers either way.
const uint8_t *find_needle_rs(const uint8_t *haystack, size_t haystack_len, const uint8_t *needle,
                              size_t needle_len);

// Locate the value substring of the `"payload"` key inside a top-level JSON
// object. On success: writes the slice length into `*out_len` and returns a
// pointer to the opening `{`. On failure: writes 0 into `*out_len` (when
// non-null) and returns NULL.
//
// Mirrors `UsbEnvelope::findPayloadSlice` byte-for-byte. The returned pointer
// indexes into `json_line` and is only valid while the caller's buffer is
// live.
const uint8_t *find_payload_slice_rs(const uint8_t *json_line, size_t line_len, size_t *out_len);

#ifdef __cplusplus
}
#endif

#endif // CANSHIFT_USB_ENVELOPE_RS_H
