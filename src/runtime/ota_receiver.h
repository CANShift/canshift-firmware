#pragma once

#include <stddef.h>
#include <stdint.h>

namespace OtaReceiver {

enum class State : uint8_t {
    Idle = 0,
    Receiving,
    Committed,
    Failed,
};

struct BeginResult {
    bool ok;
    const char *error;
};

struct WriteResult {
    bool ok;
    const char *error;
    size_t writtenTotal;
};

struct CommitResult {
    bool ok;
    const char *error;
    int detailCode;
};

[[nodiscard]] State state();

[[nodiscard]] size_t expectedSize();

[[nodiscard]] size_t writtenSize();

BeginResult begin(size_t totalSize, const uint8_t expectedSha256[32]);

WriteResult writeChunk(uint32_t offset, const uint8_t *data, size_t len);

CommitResult commit();

void abort(const char *reason);

} // namespace OtaReceiver
