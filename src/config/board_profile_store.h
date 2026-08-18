#pragma once

#include <stddef.h>

namespace BoardProfileStore {

bool loadAndApply();
bool save(const char *blob, size_t len);
bool saveBoardId(const char *boardId);

} // namespace BoardProfileStore
