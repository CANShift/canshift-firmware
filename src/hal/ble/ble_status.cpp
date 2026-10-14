#include "app_config.h"
#if APP_BLE_ENABLED

    #include "ble_server.h"
    #include "ble_json_arena.h"
    #include "ble_server_internal.h"

    #include "board.h"
    #include "diag/logger.h"
    #include "runtime/signal_store.h"
    #include "can/signal_map.h"
    #include "ui/theme_manager.h"
    #include "util/json_writer.h"

    #include <ArduinoJson.h>
    #include <NimBLEDevice.h>

namespace BleServerInternal {

void updateStatus() {

    auto *pStatus = BleServerInternal::s_pStatus;
    if (!pStatus)
        return;
    BleJsonArena::LeasedDocument leased(BleJsonArena::Slot::Status);
    JsonDocument &doc = leased.document();
    doc["ver"] = APP_VERSION_STR;
    doc["board_id"] = canshift::boards::runtimeBoardProfile().board_id;
    doc["can"] = SignalStore::isValid(SignalIds::RPM) ? 1 : 0;
    doc["is_day"] = ThemeManager::isDayMode() ? 1 : 0;
    if (doc.overflowed()) {
        LOG_WARN("BLE", "STATUS arena %s — skipping notify", leased.held() ? "full" : "busy");
        return;
    }
    char buf[128];

    const size_t len = JsonWriter::toBuffer(doc, buf, sizeof(buf));
    if (len == 0 || len >= sizeof(buf)) {
        LOG_WARN("BLE", "STATUS payload truncated (len=%u, cap=%u) — skipping notify",
                 static_cast<unsigned>(len), static_cast<unsigned>(sizeof(buf)));
        return;
    }
    pStatus->setValue(buf);
}

} // namespace BleServerInternal

void BleServer::pushStatusNotify() {
    BleServerInternal::updateStatus();
    auto *pStatus = BleServerInternal::s_pStatus;
    if (pStatus && pStatus->getSubscribedCount() > 0)
        pStatus->notify();
}

#endif
