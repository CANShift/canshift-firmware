#ifndef CANSHIFT_OTA_SIGNATURE_RS_H
#define CANSHIFT_OTA_SIGNATURE_RS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define OTA_SIGNATURE_BYTES 64
#define OTA_SIGNATURE_STATE_BYTES 576
#define OTA_SIGNATURE_STATE_ALIGN 16

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t bytes[OTA_SIGNATURE_STATE_BYTES];
} __attribute__((aligned(OTA_SIGNATURE_STATE_ALIGN))) OtaSignatureSlot;

void ota_signature_clear_rs(OtaSignatureSlot *slot);
bool ota_signature_begin_rs(OtaSignatureSlot *slot, const uint8_t *signature);
void ota_signature_update_rs(OtaSignatureSlot *slot, const uint8_t *data, size_t len);
bool ota_signature_finish_rs(OtaSignatureSlot *slot);

#ifdef __cplusplus
}

static_assert(sizeof(OtaSignatureSlot) == OTA_SIGNATURE_STATE_BYTES,
              "OtaSignatureSlot layout must match rust/ota-signature");
static_assert(alignof(OtaSignatureSlot) == OTA_SIGNATURE_STATE_ALIGN,
              "OtaSignatureSlot alignment must match rust/ota-signature");
#endif

#endif
