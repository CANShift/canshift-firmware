#ifndef CANSHIFT_WORKSHOP_MODE_RS_H
#define CANSHIFT_WORKSHOP_MODE_RS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define WORKSHOP_ENTROPY_BYTES 64
#define WORKSHOP_SSID_CAP 33
#define WORKSHOP_PASSWORD_CAP 64
#define WORKSHOP_JOIN_PAYLOAD_CAP 224

#ifdef __cplusplus
extern "C" {
#endif

enum { WORKSHOP_STATE_DRIVING = 0, WORKSHOP_STATE_ENTERING = 1, WORKSHOP_STATE_ACTIVE = 2 };

enum { WORKSHOP_EVENT_REQUEST = 0, WORKSHOP_EVENT_ENTERED = 1, WORKSHOP_EVENT_FAILED = 2 };

uint8_t workshop_next_state_rs(uint8_t state, uint8_t event, bool is_offered);
bool workshop_board_supported_rs(uint32_t filesystem_bytes);
bool workshop_offered_rs(uint32_t filesystem_bytes, bool speed_mapped, bool speed_valid,
                         float speed_kph);
bool workshop_generate_credentials_rs(const uint8_t *entropy, size_t entropy_len, char *ssid,
                                      size_t ssid_cap, char *password, size_t password_cap);
bool workshop_credentials_valid_rs(const char *ssid, const char *password);
size_t workshop_join_payload_rs(const char *ssid, const char *password, char *out, size_t out_cap);

#ifdef __cplusplus
}
#endif

#endif
