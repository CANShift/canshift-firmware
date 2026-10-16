#include "alert_broadcast.h"

#include "hal/ble/alert_frame.h"

#include <freertos/FreeRTOS.h>
#include <string.h>

namespace AlertBroadcast {

namespace {

portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
uint8_t s_frame[AlertFrame::kMaxFrameBytes] = {AlertFrame::kVersion, 0};
size_t s_len = AlertFrame::kHeaderBytes;
uint32_t s_generation = 0;

} // namespace

void publish(const uint8_t *frame, size_t len) {
    if (frame == nullptr || len < AlertFrame::kHeaderBytes || len > AlertFrame::kMaxFrameBytes)
        return;
    portENTER_CRITICAL(&s_mux);
    if (len != s_len || memcmp(frame, s_frame, len) != 0) {
        memcpy(s_frame, frame, len);
        s_len = len;
        s_generation++;
    }
    portEXIT_CRITICAL(&s_mux);
}

size_t copyLatest(uint8_t *out, size_t outSize, uint32_t *generation) {
    if (out == nullptr || generation == nullptr || outSize < AlertFrame::kMaxFrameBytes)
        return 0;
    portENTER_CRITICAL(&s_mux);
    const size_t len = s_len;
    memcpy(out, s_frame, len);
    *generation = s_generation;
    portEXIT_CRITICAL(&s_mux);
    return len;
}

} // namespace AlertBroadcast
