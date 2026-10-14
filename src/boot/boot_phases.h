#pragma once

namespace BootPhases {

void logHeap(const char *stage);

void silenceFrameworkLogNoise();

void initPsramAndLogEntry();

void initTaskWatchdog();

void initLvglMemoryPool();

[[nodiscard]] bool mountStorageOrLogError();

void provisionDefaultConfigsIfNeeded(bool storageOk);

void loadConfigWithHeapBracket();

void initBleEarlyIfEnabled();

[[nodiscard]] bool initDisplayHardware();

[[nodiscard]] bool initTouchHardware();

void initLvglFsIfStorageOk(bool storageOk);

void provisionDefaultFontsIfNeeded(bool storageOk);

void initFontManagerWithHeapLog();

void preloadIconsWithHeapLog();

} // namespace BootPhases
