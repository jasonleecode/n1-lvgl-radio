#ifndef N1_RADIO_DSP_H
#define N1_RADIO_DSP_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#define FM_TAPS 193
#define FM_PHASES 1
#define FM_INPUT_RATE 240000
#define FM_OUTPUT_RATE 48000
// FM audio: 240 kHz demodulated PCM -> anti-aliased 48 kHz mono.
typedef struct {float history[FM_TAPS],coeff[FM_PHASES][FM_TAPS];unsigned pos,phase;float deemphasis,alpha;} fm_resampler;
void fm_resampler_init(fm_resampler *s);
void fm_resampler_enable_deemphasis(fm_resampler *s);
size_t fm_resampler_process(fm_resampler *s,const int16_t *in,size_t count,int16_t *out,size_t capacity);
// Pilot-locked stereo MPX decoder. Output is interleaved L/R at 48kHz.
typedef struct {
 fm_resampler sum,difference;
 double phase,frequency;
 float pilot_i[2],pilot_q[2],power,error_power,blend,pilot_amplitude;
 unsigned divider,good,bad;
 bool locked;
} fm_stereo;
void fm_stereo_init(fm_stereo *s);
size_t fm_stereo_process(fm_stereo *s,const int16_t *in,size_t count,int16_t *out,size_t frame_capacity,bool force_mono);
#endif
