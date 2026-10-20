#include "app_config.h"
#if APP_AUDIO_ENABLED

    #include "sound.h"

    #include "board.h"
    #include "diag/logger.h"
    #include "hal/audio/i2s_tone_output.h"
    #include "runtime/sound_settings.h"
    #include "runtime/task_spawn.h"
    #include "tone_sequencer_rs.h"
    #include "ui/alert_takeover.h"
    #include "ui/rev_limit_flash.h"

    #include <Arduino.h>
    #include <esp_task_wdt.h>

namespace {

constexpr size_t kChunkFrames = 256;
constexpr size_t kStereoChannels = 2;
constexpr size_t kChunkSamples = kChunkFrames * kStereoChannels;
constexpr uint32_t kIdlePollMs = 20;
constexpr uint32_t kWriteRetryMs = 1000;

ToneSequencerRs s_sequencer = {};
ToneSynthRs s_synth = {};
int16_t s_chunk[kChunkSamples];

void renderChunk(const ToneOutputRs &tone) {
    tone_synth_fill_rs(&s_synth, s_chunk, kChunkSamples, tone);
    if (!I2sToneOutput::write(s_chunk, kChunkSamples))
        vTaskDelay(pdMS_TO_TICKS(kWriteRetryMs));
}

ToneDemandRs currentDemand() {
    const SoundSettingsWire::Values settings = SoundSettings::current();
    const bool shiftLit = RevLimitFlash::isEngaged() && RevLimitFlash::isRowLit();
    return {settings.volumePct, AlertTakeover::isActive(), settings.shiftTone && shiftLit, 0};
}

void requestBootTone() {
    if (!SoundSettings::current().boot)
        return;
    if (!tone_sequencer_request_rs(&s_sequencer, kTonePatternBoot, millis()))
        LOG_WARN("AUDIO", "boot tone refused");
}

void taskSound(void *) {
    while (true) {
        esp_task_wdt_reset();
        const ToneOutputRs tone = tone_sequencer_tick_rs(&s_sequencer, millis(), currentDemand());
        if (s_sequencer.pattern == kTonePatternNone) {
            vTaskDelay(pdMS_TO_TICKS(kIdlePollMs));
            continue;
        }
        renderChunk(tone);
    }
}

}

void Sound::start() {
    const canshift::boards::AudioProfile &audio = canshift::boards::runtimeBoardProfile().audio;
    if (!audio.present) {
        LOG_INFO("AUDIO", "board has no audio output — sound task not started");
        return;
    }
    SoundSettings::load();
    if (!I2sToneOutput::begin(audio))
        return;
    tone_sequencer_reset_rs(&s_sequencer);
    requestBootTone();
    if (TaskSpawn::spawnDynamic("sound", taskSound, TASK_STACK_SOUND, kTaskPrioSound,
                                kTaskCoreSound) == nullptr)
        LOG_ERROR("AUDIO", "sound task not started — the dash stays silent");
}

#endif
