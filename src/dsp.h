#ifndef N1_RADIO_DSP_H
#define N1_RADIO_DSP_H
#include <stdint.h>
#include <stddef.h>
#define FM_TAPS 193
#define FM_PHASES 1
#define FM_INPUT_RATE 240000
#define FM_OUTPUT_RATE 48000
// FM audio: 240 kHz demodulated PCM -> anti-aliased 48 kHz mono.
typedef struct {float history[FM_TAPS],coeff[FM_PHASES][FM_TAPS];unsigned pos,phase;} fm_resampler;
void fm_resampler_init(fm_resampler *s);
size_t fm_resampler_process(fm_resampler *s,const int16_t *in,size_t count,int16_t *out,size_t capacity);
#endif
