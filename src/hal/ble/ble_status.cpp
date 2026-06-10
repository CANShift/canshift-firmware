#include "app_config.h"
#if APP_BLE_ENABLED

    #include "ble_server.h"
    #include "ble_server_internal.h"

    #include "diag/logger.h"
    #include "runtime/signal_store.h"
    #include "can/signal_map.h"
    #include "ui/theme_manager.h"

    #include <ArduinoJson.h>
    #include <NimBLEDevice.h>

namespace BleServerInternal {

void updateStatus() {
    // Snapshot the global — stop() can null s_pStatus on another task; the
    // underlying characteristic stays alive via earlyInit()'s GATT path (#1035).
    auto *pStatus = BleServerInternal::s_pStatus;
    if (!pStatus)
        return;
    JsonDocument doc;
    doc["ver"] = APP_VERSION_STR;
    doc["can"] = SignalStore::isValid(SignalIds::RPM) ? 1 : 0;
    doc["is_day"] = ThemeManager::isDayMode() ? 1 : 0;
    char buf[128];
    // ArduinoJson truncates silently — a partial payload crashes the mobile parser (#936).
    const size_t len = serializeJson(doc, buf, sizeof(buf));
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

#endif // APP_BLE_ENABLED
