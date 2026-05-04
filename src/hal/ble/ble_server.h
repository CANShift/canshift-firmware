#pragma once
// ble_server.h — BLE GATT server for mobile app (CANShift Mobile)
//
// Service UUIDs (128-bit, CANShift vendor prefix 4fa0b6a0-*):
//   Service:   4fa0b6a0-0000-0000-0000-000000000001
//   TELE:      4fa0b6a0-0000-0000-0000-000000000002  notify — live signal JSON ~10Hz
//   STATUS:    4fa0b6a0-0000-0000-0000-000000000003  read  — firmware version + CAN health
//   SETTINGS:  4fa0b6a0-0000-0000-0000-000000000004  write — screen settings JSON
//   CMD:       4fa0b6a0-0000-0000-0000-000000000005  write — commands (start_wifi_ap, reboot)
//
// Telemetry JSON keys (compact — mobile app maps these to signal names):
//   r=RPM, tps=throttle%, map=MAP kPa, bst=boost bar, iat=intake temp,
//   ct=coolant temp, ot=oil temp, op=oil pressure, fp=fuel pressure,
//   lam=lambda, s=speed kph, g=gear, bat=battery V
//   Only valid (non-timed-out) signals are included.

#include "app_config.h"
#include <stdbool.h>

#if APP_BLE_ENABLED

namespace BleServer {

/** Initialize NimBLE stack, GATT service, and start advertising. */
void init();

/**
 * Called periodically from the BLE task (~10Hz).
 * Sends telemetry notification if a client is subscribed.
 * Also refreshes the STATUS characteristic value.
 */
void tick();

/** Returns true if a mobile client is currently connected. */
bool isConnected();

} // namespace BleServer

#endif // APP_BLE_ENABLED
