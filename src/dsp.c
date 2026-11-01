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
void fm_resampler_enable_deemphasis(fm_resampler *s) {
 s->alpha=1.0f-expf(-1.0f/(FM_INPUT_RATE*75e-6f));
}
static bool resample_one(fm_resampler *s,float sample,float *out) {
 if(s->alpha>0){s->deemphasis+=s->alpha*(sample-s->deemphasis);sample=s->deemphasis;}
 s->history[s->pos]=sample;unsigned latest=s->pos;s->pos=(s->pos+1)%FM_TAPS;
 s->phase+=FM_OUTPUT_RATE;if(s->phase<FM_INPUT_RATE)return false;s->phase-=FM_INPUT_RATE;
 float y=0;
 for(unsigned k=0,j=latest;k<FM_TAPS;k++,j=j?j-1:FM_TAPS-1)y+=s->history[j]*s->coeff[0][k];
 *out=y;return true;
}
static int16_t pcm(float y) {
 if(y>32767)y=32767;
 if(y< -32768)y=-32768;
 return (int16_t)lrintf(y);
}
size_t fm_resampler_process(fm_resampler *s,const int16_t *in,size_t count,int16_t *out,size_t capacity) {
 size_t produced=0;float y;
 for(size_t i=0;i<count;i++)if(resample_one(s,in[i],&y) && produced<capacity)out[produced++]=pcm(y);
 return produced;
}
void fm_stereo_init(fm_stereo *s) {
 memset(s,0,sizeof(*s));fm_resampler_init(&s->sum);fm_resampler_init(&s->difference);
 fm_resampler_enable_deemphasis(&s->sum);fm_resampler_enable_deemphasis(&s->difference);
 s->error_power=1;
}
size_t fm_stereo_process(fm_stereo *s,const int16_t *in,size_t count,int16_t *out,size_t capacity,bool force_mono) {
 const double nominal=2*PI*19000/FM_INPUT_RATE,limit=2*PI*30/FM_INPUT_RATE;
 const float pilot_alpha=1-expf(-2*PI*200/FM_INPUT_RATE);
 const float power_alpha=1-expf(-1.0f/(FM_INPUT_RATE*.05f));
 // Type-II PLL updated at 24kHz; 10Hz natural frequency, damping 0.707.
 const double wn=2*PI*10/(FM_INPUT_RATE/10),kp=2*.707*wn,ki=wn*wn/10;
 size_t produced=0;
 for(size_t i=0;i<count;i++) {
  float x=in[i]/32768.0f,si=sinf((float)s->phase),co=cosf((float)s->phase);
  s->pilot_i[0]+=pilot_alpha*(x*si-s->pilot_i[0]);s->pilot_q[0]+=pilot_alpha*(x*co-s->pilot_q[0]);
  s->pilot_i[1]+=pilot_alpha*(s->pilot_i[0]-s->pilot_i[1]);s->pilot_q[1]+=pilot_alpha*(s->pilot_q[0]-s->pilot_q[1]);
  s->power+=power_alpha*(x*x-s->power);
  if(++s->divider==10) {
   s->divider=0;float error=atan2f(s->pilot_q[1],s->pilot_i[1]);
   s->frequency+=ki*error;if(s->frequency>limit)s->frequency=limit;if(s->frequency< -limit)s->frequency=-limit;
   s->phase+=kp*error;
   s->error_power+=.0014f*(error*error-s->error_power);
   s->pilot_amplitude=2*hypotf(s->pilot_i[1],s->pilot_q[1]);
   bool good=s->pilot_amplitude>.0015f && s->pilot_amplitude*s->pilot_amplitude>.004f*s->power && s->error_power<.0225f && fabs(s->frequency)<2*PI*20/FM_INPUT_RATE;
   if(good){s->bad=0;if(s->good<6000)s->good++;if(s->good>=6000)s->locked=true;}
   else {s->good=0;if(s->bad<1920)s->bad++;if(s->bad>=1920)s->locked=false;}
  }
  s->phase+=nominal+s->frequency;if(s->phase>PI)s->phase-=2*PI;if(s->phase< -PI)s->phase+=2*PI;
  float sum=0,difference=0;bool ready=resample_one(&s->sum,in[i],&sum);
  resample_one(&s->difference,2*in[i]*(2*si*co),&difference);
  if(!ready)continue;
  float target=s->locked && !force_mono?1:0;
  s->blend+=(target-s->blend)*(1-expf(-1.0f/(FM_OUTPUT_RATE*.05f)));
  if(produced<capacity){out[2*produced]=pcm(sum+s->blend*difference);out[2*produced+1]=pcm(sum-s->blend*difference);produced++;}
 }
 return produced;
}
