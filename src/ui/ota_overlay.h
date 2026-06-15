#pragma once

#include <stddef.h>

namespace OtaOverlay {

void show(size_t totalBytes);

void setProgress(size_t writtenBytes);

void hide();

[[nodiscard]] bool isActive();

namespace Detail {
void tick();
}

} // namespace OtaOverlay
