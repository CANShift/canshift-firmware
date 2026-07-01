#pragma once

#include <lvgl.h>

namespace BurnOverlay {

enum class ErrorReason {
    WriteFailed,
};

void show();

void hide();

void showError(ErrorReason reason);

} // namespace BurnOverlay
