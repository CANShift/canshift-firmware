#include "app_config.h"
#if APP_AUDIO_ENABLED

    #include "i2s_tone_output.h"

    #include "diag/error_store.h"
    #include "diag/logger.h"
    #include "tone_sequencer_rs.h"

    #include <driver/i2s.h>
    #include <freertos/FreeRTOS.h>

namespace {

constexpr i2s_port_t kPort = I2S_NUM_0;
constexpr int kDmaBufferCount = 4;
constexpr int kDmaBufferFrames = 256;
constexpr uint32_t kWriteTimeoutMs = 100;

i2s_config_t toneConfig() {
    i2s_config_t cfg = {};
    cfg.mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_TX);
    cfg.sample_rate = kToneSampleRateHz;
    cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
    cfg.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
    cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    cfg.dma_buf_count = kDmaBufferCount;
    cfg.dma_buf_len = kDmaBufferFrames;
    cfg.tx_desc_auto_clear = true;
    return cfg;
}

i2s_pin_config_t tonePins(const canshift::boards::AudioProfile &audio) {
    i2s_pin_config_t pins = {};
    pins.mck_io_num = I2S_PIN_NO_CHANGE;
    pins.bck_io_num = audio.pin_bck;
    pins.ws_io_num = audio.pin_ws;
    pins.data_out_num = audio.pin_dout;
    pins.data_in_num = I2S_PIN_NO_CHANGE;
    return pins;
}

bool fail(const char *step, esp_err_t err) {
    LOG_ERROR("AUDIO", "I2S %s failed: %s", step, esp_err_to_name(err));
    ErrorStore::push(ERROR_SRC_SYSTEM, "AUDIO_I2S", step);
    return false;
}

}

bool I2sToneOutput::begin(const canshift::boards::AudioProfile &audio) {
    const i2s_config_t cfg = toneConfig();
    const esp_err_t installed = i2s_driver_install(kPort, &cfg, 0, nullptr);
    if (installed != ESP_OK)
        return fail("install", installed);
    const i2s_pin_config_t pins = tonePins(audio);
    const esp_err_t pinned = i2s_set_pin(kPort, &pins);
    if (pinned != ESP_OK) {
        if (i2s_driver_uninstall(kPort) != ESP_OK)
            LOG_WARN("AUDIO", "I2S uninstall after a pin failure failed");
        return fail("set_pin", pinned);
    }
    return true;
}

bool I2sToneOutput::write(const int16_t *samples, size_t count) {
    const size_t bytes = count * sizeof(int16_t);
    size_t written = 0;
    const esp_err_t err =
        i2s_write(kPort, samples, bytes, &written, pdMS_TO_TICKS(kWriteTimeoutMs));
    if (err != ESP_OK)
        return fail("write", err);
    if (written != bytes)
        return fail("write timeout", ESP_ERR_TIMEOUT);
    return true;
}

#endif
