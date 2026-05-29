// usb_envelope.cpp — JSON envelope helpers for the USB PUT_CONFIG path.
//
// Why this lives outside `usb_comm.cpp`: parsing a 12 KB envelope into a
// JsonDocument grew the pool to ~21 KB, which couldn't be satisfied after
// the LV_MEM_SIZE bump in #555 (issue #576). Skipping the full parse keeps
// the PUT_CONFIG path heap-allocation-free. Extracting these helpers into
// their own TU lets the host test environment exercise them in isolation
// without dragging in the rest of the USB stack (#912).

#include "usb_envelope.h"

#include <string.h>

#if USE_RUST_USB_ENVELOPE
    #include "usb_envelope_rs.h"
#endif

namespace UsbEnvelope {

const char *findNeedle(const char *haystack, size_t haystackLen, const char *needle,
                       size_t needleLen) {
#if USE_RUST_USB_ENVELOPE
    // Delegate to the Rust port. The Rust shim tightens the null-pointer
    // contract (returns nullptr for null haystack / needle) — the existing
    // Unity suite never exercised that path, so the change is observable but
    // unreachable in practice.
    return reinterpret_cast<const char *>(
        find_needle_rs(reinterpret_cast<const uint8_t *>(haystack), haystackLen,
                       reinterpret_cast<const uint8_t *>(needle), needleLen));
#else
    if (needleLen == 0 || haystackLen < needleLen)
        return nullptr;
    const size_t lastStart = haystackLen - needleLen;
    for (size_t i = 0; i <= lastStart; ++i) {
        if (memcmp(haystack + i, needle, needleLen) == 0)
            return haystack + i;
    }
    return nullptr;
#endif
}

const char *findPayloadSlice(const char *jsonLine, size_t lineLen, size_t *outLen) {
#if USE_RUST_USB_ENVELOPE
    return reinterpret_cast<const char *>(
        find_payload_slice_rs(reinterpret_cast<const uint8_t *>(jsonLine), lineLen, outLen));
#else
    if (!jsonLine || !outLen)
        return nullptr;
    *outLen = 0;

    // Plain-text needle is acceptable here: the studio always emits the
    // envelope with the literal key `"payload"` and never inside a nested
    // string by accident — the surrounding `{"cmd":2,...}` frame is fixed.
    static constexpr char kNeedle[] = "\"payload\"";
    static constexpr size_t kNeedleLen = sizeof(kNeedle) - 1;
    const char *needle = findNeedle(jsonLine, lineLen, kNeedle, kNeedleLen);
    if (!needle)
        return nullptr;

    // Skip past `"payload"` then optional whitespace then the `:` separator.
    const char *cursor = needle + kNeedleLen;
    const char *end = jsonLine + lineLen;
    while (cursor < end &&
           (*cursor == ' ' || *cursor == '\t' || *cursor == '\n' || *cursor == '\r'))
        ++cursor;
    if (cursor >= end || *cursor != ':')
        return nullptr;
    ++cursor;
    while (cursor < end &&
           (*cursor == ' ' || *cursor == '\t' || *cursor == '\n' || *cursor == '\r'))
        ++cursor;
    if (cursor >= end || *cursor != '{')
        return nullptr;

    // Brace-balance walk. Strings are honoured so we don't count `{` / `}`
    // inside JSON string values. Escapes inside strings are skipped.
    const char *valueStart = cursor;
    int depth = 0;
    bool inString = false;
    while (cursor < end) {
        const char c = *cursor;
        if (inString) {
            if (c == '\\') {
                ++cursor; // skip the escaped byte verbatim
                if (cursor < end)
                    ++cursor;
                continue;
            }
            if (c == '"')
                inString = false;
            ++cursor;
            continue;
        }
        if (c == '"') {
            inString = true;
        } else if (c == '{') {
            ++depth;
        } else if (c == '}') {
            --depth;
            if (depth == 0) {
                ++cursor;
                *outLen = static_cast<size_t>(cursor - valueStart);
                return valueStart;
            }
        }
        ++cursor;
    }
    return nullptr;
#endif
}

} // namespace UsbEnvelope
