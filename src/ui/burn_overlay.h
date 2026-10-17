#pragma once

#include <lvgl.h>

namespace BurnOverlay {

enum class ErrorReason {
    WriteFailed,
    Refused,
};

void show();

void hide();

void showError(ErrorReason reason);

[[nodiscard]] bool isActive();

} // namespace BurnOverlay
