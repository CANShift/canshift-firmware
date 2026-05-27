// canshift-firmware/src/config/parse_utils.h
#pragma once
#include <cerrno>
#include <cstdint>
#include <cstdlib>

// Returns true if the entire string was consumed as a valid number in the
// given base. Empty strings or strings with trailing garbage fail.
static inline bool parseU32Strict(const char *s, int base, uint32_t *out) {
    if (s == nullptr || *s == '\0')
        return false;
    errno = 0;
    char *end = nullptr;
    const unsigned long v = strtoul(s, &end, base);
    if (errno == ERANGE)
        return false;
    if (end == s || *end != '\0')
        return false;
    if (v > UINT32_MAX)
        return false;
    *out = static_cast<uint32_t>(v);
    return true;
}
