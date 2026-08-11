#pragma once

#include <stddef.h>
#include <stdint.h>

namespace NvsStore {

bool putBytes(const char *ns, const char *key, const void *data, size_t len);
bool putString(const char *ns, const char *key, const char *value, size_t len);
bool putBool(const char *ns, const char *key, bool value);
bool putUChar(const char *ns, const char *key, uint8_t value);
bool putUShort(const char *ns, const char *key, uint16_t value);
bool remove(const char *ns, const char *key);
uint8_t getUChar(const char *ns, const char *key, uint8_t fallback);

} // namespace NvsStore
