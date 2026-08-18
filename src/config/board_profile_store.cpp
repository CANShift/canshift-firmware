#include "config/board_profile_store.h"

#include "config/board_profile_loader.h"
#include "diag/logger.h"
#include "hal/storage/nvs_store.h"

#include "board.h"

#include <Preferences.h>
#include <string.h>

namespace {
constexpr char kNvsNamespace[] = "boardcfg";
constexpr char kNvsKey[] = "profile";
constexpr char kNvsIdKey[] = "boardid";

String readKey(const char *key) {
    Preferences prefs;
    prefs.begin(kNvsNamespace, true);
    String value = prefs.getString(key, "");
    prefs.end();
    return value;
}

bool applyStoredId() {
    const String id = readKey(kNvsIdKey);
    if (id.isEmpty())
        return false;
    if (canshift::boards::applyCatalogBoard(id.c_str())) {
        LOG_INFO("BOARDCFG", "selected catalog board '%s'", id.c_str());
        return true;
    }
    LOG_WARN("BOARDCFG", "stored board id '%s' is not in this build's catalog", id.c_str());
    return false;
}

bool applyStoredBlob() {
    const String blob = readKey(kNvsKey);
    if (blob.isEmpty())
        return false;
    if (canshift::boards::applyBoardProfileBlob(blob.c_str(), blob.length())) {
        LOG_INFO("BOARDCFG", "applied provisioned board profile '%s'",
                 canshift::boards::runtimeBoardProfile().board_id);
        return true;
    }
    LOG_WARN("BOARDCFG", "provisioned board profile invalid — using compile-time default");
    return false;
}

} // namespace

namespace BoardProfileStore {

bool loadAndApply() {
    if (applyStoredId())
        return true;
    if (applyStoredBlob())
        return true;
    LOG_INFO("BOARDCFG", "no provisioned board — using compile-time default '%s'",
             canshift::boards::runtimeBoardProfile().board_id);
    return false;
}

bool save(const char *blob, size_t len) {
    canshift::boards::BoardProfile parsed;
    char idBuf[canshift::boards::kBoardIdCapacity];
    char nameBuf[canshift::boards::kBoardNameCapacity];
    if (canshift::boards::parseBoardProfileBlob(blob, len, parsed, idBuf, sizeof idBuf, nameBuf,
                                                sizeof nameBuf) !=
        canshift::boards::BoardProfileParse::Ok) {
        return false;
    }

    if (!NvsStore::putString(kNvsNamespace, kNvsKey, blob, len))
        return false;
    if (!NvsStore::remove(kNvsNamespace, kNvsIdKey))
        LOG_WARN("BOARDCFG", "stored blob but could not clear the catalog selection");
    return true;
}

bool saveBoardId(const char *boardId) {
    if (boardId == nullptr || boardId[0] == '\0')
        return false;
    if (canshift::boards::catalogBoard(boardId, canshift::boards::kActiveBoard.chip_family) ==
        nullptr) {
        LOG_WARN("BOARDCFG", "board '%s' is not in this build's catalog", boardId);
        return false;
    }
    if (!NvsStore::putString(kNvsNamespace, kNvsIdKey, boardId, strlen(boardId)))
        return false;
    if (!NvsStore::remove(kNvsNamespace, kNvsKey))
        LOG_WARN("BOARDCFG", "stored board id but could not clear the previous profile blob");
    return true;
}

} // namespace BoardProfileStore
