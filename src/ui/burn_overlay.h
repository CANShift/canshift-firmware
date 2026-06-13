#pragma once

#include <lvgl.h>

namespace BurnOverlay {

enum class ErrorReason {
    WriteFailed,
    ReloadFailed,
};

void show();

void hide();

void showError(ErrorReason reason);

} // namespace BurnOverlay
