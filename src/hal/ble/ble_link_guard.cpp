#include "app_config.h"
#if APP_BLE_ENABLED

    #include "ble_server_internal.h"

    #include "ble_link_rs.h"
    #include "diag/error_store.h"
    #include "diag/logger.h"

    #include <Arduino.h>
    #include <NimBLEDevice.h>

namespace BleServerInternal {

namespace {

uint32_t s_connectedAtMs = 0;

struct Peer {
    bool present;
    uint16_t handle;
    bool encrypted;
    bool authenticated;
};

Peer currentPeer() {
    NimBLEServer *server = NimBLEDevice::getServer();
    if (server == nullptr || server->getConnectedCount() == 0)
        return {};
    NimBLEConnInfo info = server->getPeerInfo(0);
    return {true, info.getConnHandle(), info.isEncrypted(), info.isAuthenticated()};
}

void terminate(uint16_t handle) {
    const int rc = NimBLEDevice::getServer()->disconnect(handle);
    if (rc == 0)
        return;
    LOG_ERROR("BLE", "Peer disconnect failed (rc=%d)", rc);
    ErrorStore::push(ERROR_SRC_SYSTEM, "ble_link", "peer disconnect failed");
}

void forgetAndTerminate(const ble_gap_conn_desc &desc) {
    if (NimBLEDevice::deleteBond(NimBLEAddress(desc.peer_id_addr)))
        return;
    LOG_ERROR("BLE", "Bond removal failed — disconnecting only");
    ErrorStore::push(ERROR_SRC_SYSTEM, "ble_link", "bond removal failed");
    terminate(desc.conn_handle);
}

} // namespace

void noteConnected() {
    s_connectedAtMs = millis();
}

bool linkAdmitted() {
    const Peer peer = currentPeer();
    return peer.present && ble_link_admitted_rs(peer.encrypted, peer.authenticated);
}

void dropUnauthenticatedLink(const ble_gap_conn_desc &desc, const char *reason) {
    LOG_WARN("BLE", "%s — dropping the link", reason);
    if (desc.sec_state.bonded) {
        forgetAndTerminate(desc);
        return;
    }
    terminate(desc.conn_handle);
}

void dropOverduePeer() {
    const Peer peer = currentPeer();
    if (!peer.present)
        return;
    const uint32_t connectedMs = millis() - s_connectedAtMs;
    if (!ble_link_pairing_overdue_rs(peer.encrypted, peer.authenticated, connectedMs))
        return;
    LOG_WARN("BLE", "Peer not authenticated after %lu ms — dropping the link",
             static_cast<unsigned long>(connectedMs));
    terminate(peer.handle);
}

} // namespace BleServerInternal

#endif
