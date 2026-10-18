#include "config_loader_internal.h"

#include "diag/error_store.h"
#include "diag/logger.h"

#include <ArduinoJson.h>
#include <cstring>

namespace ConfigLoaderInternal {

namespace {

void readPageButton(JsonObjectConst src, const char *key, char *out, size_t outSize) {
    out[0] = '\0';
    const char *signal = src[key] | "";
    if (strlcpy(out, signal, outSize) < outSize)
        return;
    out[0] = '\0';
    LOG_ERROR("CFG", "dashboard.json: pageButtons.%s too long — button ignored", key);
    ErrorStore::push(ERROR_SRC_CONFIG, "PAGE_BTN", "page button signal too long");
}

}

void parsePageButtons(JsonObjectConst src, CfgPageButtons *out) {
    *out = CfgPageButtons{};
    if (src.isNull())
        return;
    readPageButton(src, "next", out->next, sizeof(out->next));
    readPageButton(src, "previous", out->previous, sizeof(out->previous));
}

}
