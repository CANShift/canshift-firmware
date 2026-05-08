// format_float.h — Float-to-string formatting without newlib's float printf
//
// Hand-rolled fixed-point formatters so the firmware doesn't drag in newlib's
// `_vfprintf_r` / `_dtoa_r` machinery (~30 KB flash). See issues #305 / #405.
//
// All callers that previously used `snprintf(buf, n, "%.Nf", val)` should
// switch to `formatFixed(buf, n, val, N)` or `formatFromSpec(buf, n, val, fmt)`
// depending on whether the format string is static or user-provided.

#pragma once

#include <stddef.h>

namespace FloatFormat {

// Format `value` with `decimals` digits after the point into `buf`.
// Behaves like `snprintf(buf, size, "%.<decimals>f", value)` for finite values.
// Handles NaN ("nan") and infinity ("inf"/"-inf"). Truncates (not rounds) on
// the boundary case where the final digit would overflow the buffer.
//
// Returns the number of characters that would have been written (excluding the
// null terminator), matching snprintf semantics. Returns 0 if `buf == nullptr`
// or `size == 0`.
//
// `decimals` is clamped to [0, 9].
size_t formatFixed(char *buf, size_t size, float value, int decimals);

// Parse a printf-style format string of the form `"<prefix>%[.N]f<suffix>"`
// (where N is 0-9, optional, defaulting to 1) and render `value` into `buf`.
//
// Designed to handle user-provided format strings from JSON dashboard configs
// (e.g. `"%.1fV"`, `"%.2f bar"`). For any unrecognized format, falls back to
// `%.1f` and writes that representation, ignoring the original spec entirely.
//
// Returns the number of characters written (excluding the null terminator).
size_t formatFromSpec(char *buf, size_t size, float value, const char *spec);

// Format `value` like printf's `"%.<sigDigits>g"` — fixed notation when the
// magnitude is reasonable, drops trailing zeros after the decimal point. Used
// by the USB telemetry payload, where the studio parses values as JSON
// numbers (so the output must always be a valid JSON number).
//
// `sigDigits` is clamped to [1, 9].
size_t formatGeneral(char *buf, size_t size, float value, int sigDigits);

} // namespace FloatFormat
