#pragma once

#include "board_profile.h"

#include <stddef.h>
#include <stdint.h>

namespace canshift::boards {

enum class BoardProfileParse : uint8_t {
    Ok,
    InvalidJson,
    NotAnObject,
    WrongMagic,
    UnsupportedVersion,
    WrongShape,
};

inline constexpr char kBoardBlobMagic[] = "CANSHIFT_BOARD";
inline constexpr uint32_t kBoardBlobFormatVersion = 1;
inline constexpr size_t kBoardIdCapacity = 32;
inline constexpr size_t kBoardNameCapacity = 64;

BoardProfileParse parseBoardProfileBlob(const char *json, size_t len, BoardProfile &out,
                                        char *idBuf, size_t idCap, char *nameBuf, size_t nameCap);

const BoardProfile &runtimeBoardProfile();
bool applyBoardProfileBlob(const char *json, size_t len);
void resetRuntimeBoardProfile();

} // namespace canshift::boards
