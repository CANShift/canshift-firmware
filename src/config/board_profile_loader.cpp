#include "config/board_profile_loader.h"

#include "board.h"
#include "config/board_profile_fields.h"

#include <ArduinoJson.h>
#include <string.h>

namespace canshift::boards {
namespace {

BoardProfile s_runtime = kActiveBoard;
char s_boardId[kBoardIdCapacity];
char s_boardName[kBoardNameCapacity];

BoardProfileParse checkEnvelope(JsonObjectConst root) {
    JsonVariantConst magic = root["magic"];
    if (!magic.is<const char *>() || strcmp(magic.as<const char *>(), kBoardBlobMagic) != 0)
        return BoardProfileParse::WrongMagic;
    JsonVariantConst version = root["formatVersion"];
    if (!version.is<uint32_t>())
        return BoardProfileParse::WrongShape;
    const uint32_t fmt = version.as<uint32_t>();
    if (fmt > kBoardBlobFormatVersion)
        return BoardProfileParse::UnsupportedVersion;
    if (fmt != kBoardBlobFormatVersion)
        return BoardProfileParse::WrongShape;
    return BoardProfileParse::Ok;
}

bool parseIdentity(JsonObjectConst p, BoardProfile &out, char *idBuf, size_t idCap, char *nameBuf,
                   size_t nameCap) {
    if (!readString(p["board_id"], idBuf, idCap) || !readString(p["board_name"], nameBuf, nameCap))
        return false;
    out.board_id = idBuf;
    out.board_name = nameBuf;
    return parseChipFamily(p["chip_family"], out.chip_family);
}

}

BoardProfileParse parseBoardProfileBlob(const char *json, size_t len, BoardProfile &out,
                                        char *idBuf, size_t idCap, char *nameBuf, size_t nameCap) {
    JsonDocument doc;
    if (deserializeJson(doc, json, len))
        return BoardProfileParse::InvalidJson;
    if (!doc.is<JsonObjectConst>())
        return BoardProfileParse::NotAnObject;
    JsonObjectConst root = doc.as<JsonObjectConst>();
    const BoardProfileParse envelope = checkEnvelope(root);
    if (envelope != BoardProfileParse::Ok)
        return envelope;
    JsonObjectConst p;
    if (!subObject(root, "profile", p) || !parseIdentity(p, out, idBuf, idCap, nameBuf, nameCap) ||
        !parseProfileSections(p, out))
        return BoardProfileParse::WrongShape;
    out.audio = kNoAudio;
    return BoardProfileParse::Ok;
}

const BoardProfile &runtimeBoardProfile() {
    return s_runtime;
}

bool applyBoardProfileBlob(const char *json, size_t len) {
    BoardProfile parsed{};
    char idBuf[kBoardIdCapacity];
    char nameBuf[kBoardNameCapacity];
    if (parseBoardProfileBlob(json, len, parsed, idBuf, sizeof idBuf, nameBuf, sizeof nameBuf) !=
        BoardProfileParse::Ok) {
        return false;
    }
    memcpy(s_boardId, idBuf, strlen(idBuf) + 1);
    memcpy(s_boardName, nameBuf, strlen(nameBuf) + 1);
    parsed.board_id = s_boardId;
    parsed.board_name = s_boardName;
    s_runtime = parsed;
    return true;
}

bool applyCatalogBoard(const char *boardId) {
    const BoardProfile *found = catalogBoard(boardId, kActiveBoard.chip_family);
    if (found == nullptr)
        return false;
    s_runtime = *found;
    return true;
}

void resetRuntimeBoardProfile() {
    s_runtime = kActiveBoard;
}

}
