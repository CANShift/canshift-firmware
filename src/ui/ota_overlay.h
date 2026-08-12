#pragma once

#include <stddef.h>
#include <stdint.h>

namespace OtaOverlay {

enum class FailReason : int8_t {
    Write = 0,
    Commit,
    Aborted,
};

void show(size_t totalBytes);

void setProgress(size_t writtenBytes);

void showComplete();

void showFailed(FailReason reason, uint32_t detailCode);

void hide();

[[nodiscard]] bool isActive();

namespace Detail {
void tick();
}

} // namespace OtaOverlay
