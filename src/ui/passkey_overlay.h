#pragma once

#include <lvgl.h>
#include <stdint.h>

namespace PasskeyOverlay {

void request(uint32_t passkey);

void update(bool screenClaimed);

void hide();

}
