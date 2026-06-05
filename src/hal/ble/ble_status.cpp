// ble_status.cpp — STATUS characteristic payload builder + notify helper.
//
// Split out of `ble_server.cpp` per the canshift-firmware Medium-Refactor
// entry in #1207 ("BLE transport vs telemetry"). The STATUS characteristic
// surfaces firmware version + CAN health + day/night + WiFi AP state to
// subscribers; this TU owns the payload encoding and the
// `BleServer::pushStatusNotify()` public-API implementation.
//
// Lifetime: the STATUS characteristic pointer (`s_pStatus`) lives in
// `ble_server.cpp` — this TU snapshots it locally per call to dodge the race
// against `BleServer::stop()` documented in #1283 / #1035.

#include "app_config.h"
#if APP_BLE_ENABLED

    #include "ble_server.h"
    #include "ble_server_internal.h"

    #include "diag/logger.h"
    #include "hal/wifi/wifi_ap.h"
    #include "runtime/signal_store.h"
    #include "can/signal_map.h"
    #include "ui/theme_manager.h"

    #include <ArduinoJson.h>
    #include <NimBLEDevice.h>

namespace BleServerInternal {

void updateStatus() {
    // Snapshot the file-scope pointer to a local — BleServer::stop() can null
    // s_pStatus on a different task between the entry check and the setValue
    // call below. The earlyInit() GATT-preserved path keeps the underlying
    // characteristic object alive for the lifetime of the process, so the
    // snapshot remains valid even if the global is cleared mid-call (#1035).
    auto *pStatus = BleServerInternal::s_pStatus;
    if (!pStatus)
        return;
    JsonDocument doc;
    doc["ver"] = APP_VERSION_STR;
    doc["can"] = SignalStore::isValid(SignalIds::RPM) ? 1 : 0;
    doc["is_day"] = ThemeManager::isDayMode() ? 1 : 0;
    if (WifiAp::isActive()) {
        doc["ap_ssid"] = WifiAp::getSsid();
        // ap_password intentionally NOT included here — STATUS notifies are
        // visible to every subscriber, and the password used to ride that
        // stream in cleartext (#873). Paired+encrypted mobile clients read
        // the password by GATTing the dedicated AP_PWD characteristic.
    }
    char buf[128];
    // ArduinoJson silently truncates when the output buffer is too small;
    // a truncated STATUS payload is invalid JSON and crashes the mobile
    // parser. Detect and skip rather than push junk over the wire (#936).
    const size_t len = serializeJson(doc, buf, sizeof(buf));
    if (len == 0 || len >= sizeof(buf)) {
        LOG_WARN("BLE", "STATUS payload truncated (len=%u, cap=%u) — skipping notify",
                 static_cast<unsigned>(len), static_cast<unsigned>(sizeof(buf)));
        return;
    }
    pStatus->setValue(buf);
}

} // namespace BleServerInternal

// ---------------------------------------------------------------------------
// Public API — STATUS push
// ---------------------------------------------------------------------------

void BleServer::pushStatusNotify() {
    BleServerInternal::updateStatus();
    auto *pStatus = BleServerInternal::s_pStatus;
    if (pStatus && pStatus->getSubscribedCount() > 0)
        pStatus->notify();
}

#endif // APP_BLE_ENABLED
