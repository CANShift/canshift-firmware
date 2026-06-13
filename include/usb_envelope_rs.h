
#ifndef CANSHIFT_USB_ENVELOPE_RS_H
#define CANSHIFT_USB_ENVELOPE_RS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

const uint8_t *find_needle_rs(const uint8_t *haystack, size_t haystack_len, const uint8_t *needle,
                              size_t needle_len);

const uint8_t *find_payload_slice_rs(const uint8_t *json_line, size_t line_len, size_t *out_len);

#ifdef __cplusplus
}
#endif

#endif
