#pragma once

namespace IconAssets {

const void *resolveSource(const char *iconName);

const char *path(const char *iconName);

bool exists(const char *lvglPath);

void preload(const char *lvglPath);

void preloadDashboardAssets();

}
