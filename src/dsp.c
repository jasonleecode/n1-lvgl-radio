#include "dsp.h"
#include <string.h>
#include <math.h>
#define PI 3.14159265358979323846
void fm_resampler_init(fm_resampler *s) {
 memset(s,0,sizeof(*s));
 for(unsigned p=0;p<FM_PHASES;p++) {
  double sum=0;
  for(unsigned k=0;k<FM_TAPS;k++) {
   double x=(double)k-(FM_TAPS-1)/2.0-(double)p/FM_PHASES;
   double cutoff=14000.0/FM_INPUT_RATE;
   double h=fabs(x)<1e-12?2*cutoff:sin(2*PI*cutoff*x)/(PI*x);
   double window=.42-.5*cos(2*PI*k/(FM_TAPS-1))+.08*cos(4*PI*k/(FM_TAPS-1));
   s->coeff[p][k]=(float)(h*window);sum+=h*window;
  }
  for(unsigned k=0;k<FM_TAPS;k++)s->coeff[p][k]/=(float)sum;
 }
}
size_t fm_resampler_process(fm_resampler *s,const int16_t *in,size_t count,int16_t *out,size_t capacity) {
 size_t produced=0;
 for(size_t i=0;i<count;i++) {
  s->history[s->pos]=(float)in[i];unsigned latest=s->pos;s->pos=(s->pos+1)%FM_TAPS;
  s->phase+=FM_OUTPUT_RATE;if(s->phase<FM_INPUT_RATE)continue;s->phase-=FM_INPUT_RATE;
  unsigned phase=s->phase/(FM_OUTPUT_RATE/FM_PHASES);float y=0;
  for(unsigned k=0,j=latest;k<FM_TAPS;k++,j=j?j-1:FM_TAPS-1)y+=s->history[j]*s->coeff[phase][k];
  if(y>32767)y=32767;
  if(y< -32768)y=-32768;
  if(produced<capacity)out[produced++]=(int16_t)lrintf(y);
 }
 return produced;
}
