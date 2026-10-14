#pragma once

namespace WorkshopMode {

[[nodiscard]] bool boardSupported();

[[nodiscard]] bool offered();

[[nodiscard]] bool request();

[[nodiscard]] bool isEntering();

[[nodiscard]] bool isActive();

void settleEntry(bool entered);

} // namespace WorkshopMode
