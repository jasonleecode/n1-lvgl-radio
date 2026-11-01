#define _POSIX_C_SOURCE 200809L
#include "radio.h"
#include <stdio.h>
#include <time.h>
#include <string.h>
static void delay(void){struct timespec t={0,100000000};nanosleep(&t,NULL);}
static int rapid_check(void) {
 radio_config c;config_defaults(&c);c.hz=97400000;c.mute=true;radio_engine e;
 if(!engine_init(&e,&c))return 1;
 int failed=0;
 const int intervals[]={30,100,300,600};
 for(unsigned b=0;b<sizeof(intervals)/sizeof(intervals[0]);b++) {
  for(int i=0;i<24;i++) {
   c.hz=97400000+(i%7)*100000;engine_apply(&e,&c,true);
   struct timespec t={0,intervals[b]*1000000};nanosleep(&t,NULL);
  }
  c.hz=97400000;engine_apply(&e,&c,true);
  bool active=false,live=false;float level=0;char message[256];
  for(int i=0;i<100;i++){delay();engine_snapshot(&e,&active,&live,message,sizeof(message),&level);if(live||!active)break;}
  if(!live){fprintf(stderr,"FAIL burst %dms: %s\n",intervals[b],message);failed=1;break;}
  uint64_t before,after;pthread_mutex_lock(&e.lock);before=e.audio_samples;pthread_mutex_unlock(&e.lock);
  for(int i=0;i<10;i++)delay();
  pthread_mutex_lock(&e.lock);after=e.audio_samples;pthread_mutex_unlock(&e.lock);
  printf("Rapid tune %dms, final 97.4MHz: %llu PCM samples in 1s\n",intervals[b],(unsigned long long)(after-before));fflush(stdout);
  if(after==before){failed=1;break;}
 }
 engine_destroy(&e);return failed;
}
int main(int argc,char **argv) {
 if(argc==2 && !strcmp(argv[1],"--rapid"))return rapid_check();
 radio_config c;config_defaults(&c);c.mute=true;radio_engine e;
 if(!engine_init(&e,&c))return 1;
 radio_mode modes[]={MODE_WFM,MODE_AM,MODE_USB,MODE_LSB,MODE_NFM};int failed=0;
 for(unsigned i=0;i<sizeof(modes)/sizeof(modes[0]);i++) {
  c.mode=modes[i];c.hz=c.mode==MODE_WFM?100000000:c.mode==MODE_AM?118000000:27185000;
  engine_apply(&e,&c,true);bool live=false,active=false;float level;char message[256];
  for(int n=0;n<80;n++){delay();engine_snapshot(&e,&active,&live,message,sizeof(message),&level);if(live||!active)break;}
  if(!live){fprintf(stderr,"FAIL %s: %s\n",radio_mode_name(c.mode),message);failed=1;break;}
  uint64_t before,after;pthread_mutex_lock(&e.lock);before=e.audio_samples;pthread_mutex_unlock(&e.lock);
  for(int n=0;n<10;n++)delay();
  pthread_mutex_lock(&e.lock);after=e.audio_samples;pthread_mutex_unlock(&e.lock);
  printf("%s %lld Hz: PCM samples in 1s = %llu\n",radio_mode_name(c.mode),(long long)c.hz,(unsigned long long)(after-before));fflush(stdout);
  if(after==before){failed=1;break;}
 }
 engine_destroy(&e);return failed;
}
