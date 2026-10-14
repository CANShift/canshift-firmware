#pragma once

#include <cstdint>

namespace BenchCanTx {

using Sink = void (*)(uint32_t id, const uint8_t *data, uint8_t length);

void setSink(Sink sink);

void emit(uint32_t id, const uint8_t *data, uint8_t length);

} // namespace BenchCanTx
