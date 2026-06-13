#pragma once

#include <stdint.h>
#include <stddef.h>

namespace CanParser {

void parseFrame(uint32_t frameId, const uint8_t *data, uint8_t length);

void loadSignalDefinitions();

namespace detail {

float decodeBytes(const uint8_t *data, uint8_t startByte, uint8_t byteLen, bool bigEndian,
                  bool isSigned, uint8_t bitMask, float scale, float offset);
}

} // namespace CanParser
