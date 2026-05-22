#pragma once
// wifi_tcp.h — WiFi TCP server for Studio connection (issue #1071)
//
// Exposes the USB JSON-lines protocol over a raw TCP socket on port 5050 so
// Studio can connect over WiFi instead of (or in addition to) USB. Studio
// resolves the dash via mDNS as `canshift.local`; see hal/wifi/wifi_ap.cpp
// for the mDNS announce.
//
// Lifecycle: start()/stop() are driven by WifiAp::start()/stop() — the TCP
// server only runs while the AP is up. Single client at a time; a second
// concurrent connection is dropped at accept() time.
//
// Protocol: byte-for-byte identical to USB. Each command/response/telemetry
// message is one JSON object terminated by '\n'. Commands are dispatched
// through UsbComm::handleLine(); proactive telemetry mirroring is wired by
// installing this module's write sink as UsbComm::setAuxSink() while a
// client is connected.

#include "app_config.h"

#if APP_BLE_ENABLED && APP_WIFI_OTA_ENABLED

namespace WifiTcpServer {

/** Start the TCP server task on port 5050. No-op if already active. */
void start();

/** Signal the server task to stop. The task closes the socket and exits. */
void stop();

/** Returns true if the server task is currently running. */
bool isActive();

/** Returns true if a client is currently connected. */
bool hasClient();

} // namespace WifiTcpServer

#endif // APP_BLE_ENABLED && APP_WIFI_OTA_ENABLED
