#ifndef CANSHIFT_BLE_LINK_RS_H
#define CANSHIFT_BLE_LINK_RS_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

bool ble_link_admitted_rs(bool encrypted, bool authenticated);
bool ble_link_subscription_refused_rs(bool encrypted, bool authenticated);
bool ble_link_pairing_overdue_rs(bool encrypted, bool authenticated, uint32_t connected_ms);

#ifdef __cplusplus
}
#endif

#endif
