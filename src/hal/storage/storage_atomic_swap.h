#pragma once

#include "config/config_types.h"

#include <stddef.h>

namespace StorageAtomicSwap {

constexpr size_t kAtomicSuffixLen = 4;
constexpr size_t kSuffixedPathLen = kCfgMaxPathLen + kAtomicSuffixLen + 1;

[[nodiscard]] bool buildSuffixedPath(char *out, size_t outLen, const char *base,
                                     const char *suffix);
[[nodiscard]] bool finalize(const char *path);

}
