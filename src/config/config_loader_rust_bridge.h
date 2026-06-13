#pragma once

#include <climits>
#include <cstdlib>

#if USE_RUST_CONFIG_LOADER
    #include "config_loader_rs.h"
#endif

namespace ConfigLoaderInternal {

[[nodiscard]] inline int parseMajorVersion(const char *version) {
#if USE_RUST_CONFIG_LOADER
    return parse_major_version_rs(version);
#else
    if (!version || version[0] == '\0')
        return -1;
    char *end = nullptr;
    const long major = strtol(version, &end, 10);
    if (end == version || major < 0 || major > INT_MAX)
        return -1;
    return static_cast<int>(major);
#endif
}

} // namespace ConfigLoaderInternal
