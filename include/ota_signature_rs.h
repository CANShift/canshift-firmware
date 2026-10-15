#ifndef CANSHIFT_OTA_SIGNATURE_RS_H
#define CANSHIFT_OTA_SIGNATURE_RS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

inline constexpr int kOtaSignatureBytes = 64;
inline constexpr int kOtaSignatureStateBytes = 576;
inline constexpr int kOtaSignatureStateAlign = 16;

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t bytes[kOtaSignatureStateBytes];
} __attribute__((aligned(kOtaSignatureStateAlign))) OtaSignatureSlot;

void ota_signature_clear_rs(OtaSignatureSlot *slot);
bool ota_signature_begin_rs(OtaSignatureSlot *slot, const uint8_t *signature);
void ota_signature_update_rs(OtaSignatureSlot *slot, const uint8_t *data, size_t len);
bool ota_signature_finish_rs(OtaSignatureSlot *slot);

#ifdef __cplusplus
}

static_assert(sizeof(OtaSignatureSlot) == kOtaSignatureStateBytes,
              "OtaSignatureSlot layout must match rust/ota-signature");
static_assert(alignof(OtaSignatureSlot) == kOtaSignatureStateAlign,
              "OtaSignatureSlot alignment must match rust/ota-signature");
#endif

#endif
