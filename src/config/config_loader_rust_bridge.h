#pragma once

#include "config_loader_rs.h"

namespace ConfigLoaderInternal {

[[nodiscard]] inline int parseMajorVersion(const char *version) {
    return parse_major_version_rs(version);
}

} // namespace ConfigLoaderInternal
