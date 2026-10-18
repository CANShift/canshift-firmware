#include "app_config.h"
#if APP_BLE_ENABLED

    #include "ble_server_internal.h"

    #include "hal/ble/alert_frame.h"
    #include "runtime/alert_broadcast.h"

    #include <NimBLEDevice.h>
    #include <stddef.h>
    #include <stdint.h>

namespace {

AlertFrame::Cadence s_cadence = {};

size_t s_lastSubCount = 0;

}

namespace BleServerInternal {

void emitAlert() {
    auto *pAlert = BleServerInternal::s_pAlert;
    if (!pAlert)
        return;
    const size_t subs = pAlert->getSubscribedCount();
    if (subs > s_lastSubCount)
        AlertFrame::resetCadence(s_cadence);
    s_lastSubCount = subs;

    uint8_t buf[AlertFrame::kMaxFrameBytes];
    uint32_t generation = 0;
    const size_t len = AlertBroadcast::copyLatest(buf, sizeof(buf), &generation);
    if (len == 0 || !AlertFrame::shouldSend(s_cadence, generation))
        return;
    pAlert->setValue(buf, len);
    if (subs > 0)
        pAlert->notify();
}

}

#endif
