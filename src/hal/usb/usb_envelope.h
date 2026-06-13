#pragma once

#include <stddef.h>

namespace UsbEnvelope {

const char *findNeedle(const char *haystack, size_t haystackLen, const char *needle,
                       size_t needleLen);

const char *findPayloadSlice(const char *jsonLine, size_t lineLen, size_t *outLen);

} // namespace UsbEnvelope
