#include "nvs_store.h"

#include <Preferences.h>

namespace {

template <typename WriteFn>
bool withWritableNamespace(const char *ns, WriteFn write) {
    Preferences prefs;
    if (!prefs.begin(ns, false)) {
        return false;
    }
    const bool ok = write(prefs);
    prefs.end();
    return ok;
}

}

namespace NvsStore {

bool putBytes(const char *ns, const char *key, const void *data, size_t len) {
    return withWritableNamespace(ns,
                                 [&](Preferences &p) { return p.putBytes(key, data, len) == len; });
}

bool putString(const char *ns, const char *key, const char *value, size_t len) {
    return withWritableNamespace(ns,
                                 [&](Preferences &p) { return p.putString(key, value) == len; });
}

bool putBool(const char *ns, const char *key, bool value) {
    return withWritableNamespace(ns, [&](Preferences &p) { return p.putBool(key, value) == 1U; });
}

bool putUChar(const char *ns, const char *key, uint8_t value) {
    return withWritableNamespace(ns, [&](Preferences &p) { return p.putUChar(key, value) == 1U; });
}

bool putUShort(const char *ns, const char *key, uint16_t value) {
    return withWritableNamespace(
        ns, [&](Preferences &p) { return p.putUShort(key, value) == sizeof(uint16_t); });
}

bool remove(const char *ns, const char *key) {
    return withWritableNamespace(ns,
                                 [&](Preferences &p) { return !p.isKey(key) || p.remove(key); });
}

uint8_t getUChar(const char *ns, const char *key, uint8_t fallback) {
    Preferences prefs;
    if (!prefs.begin(ns, true)) {
        return fallback;
    }
    const uint8_t value = prefs.getUChar(key, fallback);
    prefs.end();
    return value;
}

bool putULong(const char *ns, const char *key, uint32_t value) {
    return withWritableNamespace(
        ns, [&](Preferences &p) { return p.putULong(key, value) == sizeof(uint32_t); });
}

bool getULong(const char *ns, const char *key, uint32_t &value) {
    Preferences prefs;
    if (!prefs.begin(ns, true)) {
        return false;
    }
    const bool present = prefs.isKey(key);
    if (present) {
        value = prefs.getULong(key, 0);
    }
    prefs.end();
    return present;
}

bool getBytes(const char *ns, const char *key, void *data, size_t len) {
    Preferences prefs;
    if (!prefs.begin(ns, true)) {
        return false;
    }
    const bool exact = prefs.isKey(key) && prefs.getBytesLength(key) == len &&
                       prefs.getBytes(key, data, len) == len;
    prefs.end();
    return exact;
}

}
