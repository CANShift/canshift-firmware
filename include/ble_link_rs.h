#ifndef CANSHIFT_BLE_LINK_RS_H
#define CANSHIFT_BLE_LINK_RS_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t window_start_ms;
    uint8_t prompts;
    uint8_t _pad[3];
} BleLinkPasskeyBudgetRs;

bool ble_link_admitted_rs(bool encrypted, bool authenticated);
bool ble_link_subscription_refused_rs(bool encrypted, bool authenticated);
bool ble_link_pairing_overdue_rs(bool encrypted, bool authenticated, uint32_t connected_ms);
bool ble_link_passkey_prompt_admit_rs(BleLinkPasskeyBudgetRs *budget, uint32_t now_ms);

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
static_assert(sizeof(BleLinkPasskeyBudgetRs) == 8,
              "BleLinkPasskeyBudgetRs layout must match rust/ble-link");
#endif

#endif
