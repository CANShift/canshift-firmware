#include "usb_envelope.h"
#include "usb_envelope_rs.h"

#include <stddef.h>
#include <stdint.h>

namespace UsbEnvelope {

const char *findNeedle(const char *haystack, size_t haystackLen, const char *needle,
                       size_t needleLen) {
    return reinterpret_cast<const char *>(
        find_needle_rs(reinterpret_cast<const uint8_t *>(haystack), haystackLen,
                       reinterpret_cast<const uint8_t *>(needle), needleLen));
}

const char *findPayloadSlice(const char *jsonLine, size_t lineLen, size_t *outLen) {
    return reinterpret_cast<const char *>(
        find_payload_slice_rs(reinterpret_cast<const uint8_t *>(jsonLine), lineLen, outLen));
}

} // namespace UsbEnvelope
