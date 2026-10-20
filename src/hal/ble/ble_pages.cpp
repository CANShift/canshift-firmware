#include "app_config.h"
#if APP_BLE_ENABLED

    #include "ble_server.h"
    #include "ble_json_arena.h"
    #include "ble_server_internal.h"

    #include "diag/logger.h"
    #include "runtime/page_broadcast.h"
    #include "util/json_writer.h"

    #include <ArduinoJson.h>
    #include <NimBLEDevice.h>
    #include <string.h>

namespace {

constexpr size_t kPagesPayloadCap = 320;
constexpr char kPagesTruncatedReply[] = "{\"err\":\"too_long\"}";

char s_notifiedPageId[kCfgMaxIdLen] = {};

class PagesCallbacks : public NimBLECharacteristicCallbacks {
    void onRead(NimBLECharacteristic *pChar) override {
        BleJsonArena::LeasedDocument leased(BleJsonArena::Slot::Callback);
        JsonDocument &doc = leased.document();
        PageBroadcast::writePageList(doc.to<JsonObject>(), PageBroadcast::latest());
        char buf[kPagesPayloadCap];
        const size_t len = JsonWriter::toBuffer(doc, buf, sizeof(buf));
        if (doc.overflowed() || len == 0 || len >= sizeof(buf)) {
            LOG_WARN("BLE", "PAGES read payload truncated (len=%u, cap=%u) — emitting canary",
                     static_cast<unsigned>(len), static_cast<unsigned>(sizeof(buf)));
            pChar->setValue(kPagesTruncatedReply);
            return;
        }
        pChar->setValue(buf);
    }
};

}

namespace BleServerInternal {

NimBLECharacteristicCallbacks &pagesCallbacks() {
    static PagesCallbacks s_callbacks;
    return s_callbacks;
}

void emitPageChange() {
    const PageBroadcast::Snapshot snapshot = PageBroadcast::latest();
    const char *current = PageBroadcast::currentId(snapshot);
    if (strcmp(current, s_notifiedPageId) == 0)
        return;
    strlcpy(s_notifiedPageId, current, sizeof(s_notifiedPageId));
    BleServer::pushStatusNotify();
}

}

#endif
