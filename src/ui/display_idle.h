#pragma once

namespace DisplayIdle {

void init();

void update();

void wake();

[[nodiscard]] bool isPanelOff();

}
