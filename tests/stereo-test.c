#include "dsp.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define PI 3.14159265358979323846
static unsigned rng=1;
static float noise(void){rng=rng*1664525u+1013904223u;return ((rng>>8)/16777216.f-.5f)*2;}
static double run(double offset,bool pilot,bool mono,bool force,bool right_only) {
 fm_stereo s;fm_stereo_init(&s);int16_t in[2400],out[960];double left=0,right=0,cross=0;unsigned total=0;
 for(int block=0;block<250;block++) {
  for(int i=0;i<2400;i++) {
   double t=(block*2400+i)/240000.,phase=2*PI*(19000+offset)*t+.73;
   double tone=6000*sin(2*PI*1000*t),l=right_only?0:tone,r=mono?tone:(right_only?tone:0);
   in[i]=(int16_t)(.5*(l+r)+.5*(l-r)*sin(2*phase)+(pilot?1600*sin(phase):0)+12*noise());
  }
  size_t frames=fm_stereo_process(&s,in,2400,out,480,force);assert(frames==480);
  if(block>150)for(size_t i=0;i<frames;i++){left+=(double)out[2*i]*out[2*i];right+=(double)out[2*i+1]*out[2*i+1];cross+=abs(out[2*i]-out[2*i+1]);total++;}
 }
 if(pilot){assert(s.locked);assert(force?s.blend<.001:s.blend>.99);}else {assert(!s.locked);assert(s.blend<.001);}
 double separation=10*log10((left+1)/(right+1));
 if(!pilot || force)assert(cross/total<2);else if(mono)assert(cross/total<10);else {assert(right_only?separation< -30:separation>30);assert(sqrt((right_only?right:left)/total)>3500);}
 printf("offset %.1fHz pilot %d mono %d forced %d: locked %d blend %.3f separation %.1fdB\n",offset,pilot,mono,force,s.locked,s.blend,separation);
 // Removing pilot must unlock and smoothly return to identical channels.
 memset(in,0,sizeof(in));for(int n=0;n<150;n++)fm_stereo_process(&s,in,2400,out,480,false);
 assert(!s.locked && s.blend<.001);
 return separation;
}
int main(void) {
 run(0,true,false,false,false);run(2,true,false,false,false);run(-2,true,false,false,false);run(0,true,false,false,true);
 run(0,false,true,false,false);run(0,true,true,false,false);run(0,true,false,true,false);
 // Broadband noise must not be mistaken for a coherent pilot.
 fm_stereo s;fm_stereo_init(&s);int16_t in[2400],out[960];
 for(int b=0;b<150;b++){for(int i=0;i<2400;i++)in[i]=(int16_t)(10000*noise());fm_stereo_process(&s,in,2400,out,480,false);}assert(!s.locked);
 // Arbitrary chunks preserve PLL, channel ordering and decimation exactly.
 int16_t input[24000],a[9600],b[9600];for(int i=0;i<24000;i++)input[i]=(int16_t)(10000*noise());
 fm_stereo x,y;fm_stereo_init(&x);fm_stereo_init(&y);assert(fm_stereo_process(&x,input,24000,a,4800,false)==4800);size_t n=0;
 for(int pos=0;pos<24000;){int k=pos%73+1;if(k+pos>24000)k=24000-pos;n+=fm_stereo_process(&y,input+pos,k,b+2*n,4800-n,false);pos+=k;}
 assert(n==4800 && !memcmp(a,b,sizeof(a)));puts("PASS: stereo separation, pilot drift/loss, mono fallback, forced mono, noise rejection and streaming");return 0;
}
