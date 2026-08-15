#pragma once

#include <stddef.h>
#include <stdio.h>

namespace TextJoin {

constexpr const char *kSeparator = " · ";

inline bool append(char *buf, size_t len, size_t &used, const char *part) {
    const int n = snprintf(buf + used, len - used, "%s%s", (used > 0) ? kSeparator : "", part);
    if (n < 0 || static_cast<size_t>(n) >= len - used)
        return false;
    used += static_cast<size_t>(n);
    return true;
}

} // namespace TextJoin
