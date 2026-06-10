// Heap-free brace walk over the PUT_CONFIG envelope — full JSON parse would
// grow the pool to ~21 KB and OOM after the LV_MEM_SIZE bump (#555 / #576).
#include "usb_envelope.h"

#include <string.h>

#if USE_RUST_USB_ENVELOPE
    #include "usb_envelope_rs.h"
#endif

namespace UsbEnvelope {

const char *findNeedle(const char *haystack, size_t haystackLen, const char *needle,
                       size_t needleLen) {
#if USE_RUST_USB_ENVELOPE
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

    // Plain-text needle — the envelope frame `{"cmd":2,...}` is fixed.
    static constexpr char kNeedle[] = "\"payload\"";
    static constexpr size_t kNeedleLen = sizeof(kNeedle) - 1;
    const char *needle = findNeedle(jsonLine, lineLen, kNeedle, kNeedleLen);
    if (!needle)
        return nullptr;

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

    // Brace walk — honours strings + escapes.
    const char *valueStart = cursor;
    int depth = 0;
    bool inString = false;
    while (cursor < end) {
        const char c = *cursor;
        if (inString) {
            if (c == '\\') {
                ++cursor;
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
