#include "boards/catalog.h"

#include <string.h>

namespace canshift::boards {

const BoardProfile *catalogBoard(const char *boardId, ChipFamily chipFamily) {
    if (boardId == nullptr || boardId[0] == '\0')
        return nullptr;
    for (const BoardProfile *profile : kCatalog) {
        if (profile->chip_family != chipFamily)
            continue;
        if (strcmp(profile->board_id, boardId) == 0)
            return profile;
    }
    return nullptr;
}

} // namespace canshift::boards
