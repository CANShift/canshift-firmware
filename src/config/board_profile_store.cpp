#include "config/board_profile_store.h"

#include "config/board_profile_loader.h"
#include "diag/logger.h"
#include "hal/storage/nvs_store.h"

#include <Preferences.h>

namespace {
constexpr char kNvsNamespace[] = "boardcfg";
constexpr char kNvsKey[] = "profile";
} // namespace

namespace BoardProfileStore {

bool loadAndApply() {
    Preferences prefs;
    prefs.begin(kNvsNamespace, true);
    String blob = prefs.getString(kNvsKey, "");
    prefs.end();

    if (blob.isEmpty()) {
        LOG_INFO("BOARDCFG", "no provisioned board profile — using compile-time default");
        return false;
    }
    if (canshift::boards::applyBoardProfileBlob(blob.c_str(), blob.length())) {
        LOG_INFO("BOARDCFG", "applied provisioned board profile '%s'",
                 canshift::boards::runtimeBoardProfile().board_id);
        return true;
    }
    LOG_WARN("BOARDCFG", "provisioned board profile invalid — using compile-time default");
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

    return NvsStore::putString(kNvsNamespace, kNvsKey, blob, len);
}

} // namespace BoardProfileStore
