#ifndef CANSHIFT_TONE_SEQUENCER_RS_H
#define CANSHIFT_TONE_SEQUENCER_RS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

inline constexpr uint32_t kToneSampleRateHz = 44100;

#ifdef __cplusplus
extern "C" {
#endif

enum {
    kTonePatternNone = 0,
    kTonePatternCritical = 1,
    kTonePatternBoot = 2,
    kTonePatternCount = 3
};

typedef struct {
    uint32_t step_started_ms;
    uint8_t pattern;
    uint8_t step;
    uint8_t _pad[2];
} ToneSequencerRs;

typedef struct {
    uint16_t freq_hz;
    int16_t amplitude;
} ToneOutputRs;

typedef struct {
    uint32_t phase;
} ToneSynthRs;

void tone_sequencer_reset_rs(ToneSequencerRs *seq);
bool tone_sequencer_request_rs(ToneSequencerRs *seq, uint8_t pattern, uint32_t now_ms);
ToneOutputRs tone_sequencer_tick_rs(ToneSequencerRs *seq, uint32_t now_ms, bool critical);
void tone_synth_fill_rs(ToneSynthRs *synth, int16_t *out, size_t samples, ToneOutputRs tone);

#ifdef __cplusplus
}

static_assert(sizeof(ToneSequencerRs) == 8, "ToneSequencerRs layout must match rust ToneSequencer");
static_assert(sizeof(ToneOutputRs) == 4, "ToneOutputRs layout must match rust ToneOutput");
static_assert(sizeof(ToneSynthRs) == 4, "ToneSynthRs layout must match rust ToneSynth");
#endif

#endif
