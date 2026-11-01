#include "dsp.h"
#include <assert.h>
#include <stdbool.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define PI 3.14159265358979323846
static double level(double hz,bool deemphasis) {
 fm_resampler s;fm_resampler_init(&s);if(deemphasis)fm_resampler_enable_deemphasis(&s);int16_t in[2400],out[500];double energy=0;size_t total=0;
 for(int block=0;block<100;block++) {
  for(int i=0;i<2400;i++)in[i]=(int16_t)(10000*sin(2*PI*hz*(block*2400+i)/240000));
  size_t n=fm_resampler_process(&s,in,2400,out,500);assert(n==480);
  if(block>1)for(size_t i=0;i<n;i++){energy+=(double)out[i]*out[i];total++;}
 }
 return sqrt(energy/total);
}
int main(void) {
 double pass=level(1000,false),pilot=level(19000,false),stereo=level(38000,false);
 assert(pass>6900 && pass<7200);
 double deemp=level(1000,true),expected=pass/sqrt(1+pow(2*PI*1000*75e-6,2));assert(fabs(deemp-expected)/expected<.01);assert(pilot<15);assert(stereo<15);
 // Chunk boundaries must not affect the stream or sample count.
 fm_resampler a,b;fm_resampler_init(&a);fm_resampler_init(&b);int16_t in[24000],x[4800],y[4800];
 for(int i=0;i<24000;i++)in[i]=(int16_t)(11000*sin(2*PI*3100*i/240000));
 assert(fm_resampler_process(&a,in,24000,x,4800)==4800);size_t out=0;
 for(int pos=0;pos<24000;){int n=pos%73+1;if(pos+n>24000)n=24000-pos;out+=fm_resampler_process(&b,in+pos,n,y+out,4800-out);pos+=n;}
 assert(out==4800 && !memcmp(x,y,sizeof(x)));
 printf("PASS: 1kHz RMS %.2f, 19kHz pilot %.3f, 38kHz stereo %.3f; streaming chunks identical\n",pass,pilot,stereo);return 0;
}
