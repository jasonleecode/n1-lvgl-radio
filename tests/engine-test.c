#define _POSIX_C_SOURCE 200809L
#include "radio.h"
#include <pulse/simple.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <time.h>
// Deterministic audio sink: exercise the real receiver worker/process lifecycle.
struct pa_simple {int test;};
pa_simple *pa_simple_new(const char *a,const char *b,pa_stream_direction_t c,const char *d,const char *e,const pa_sample_spec *f,const pa_channel_map *g,const pa_buffer_attr *h,int *i){(void)a;(void)b;(void)c;(void)d;(void)e;assert(f->channels==2);(void)g;(void)h;(void)i;return calloc(1,sizeof(pa_simple));}
int pa_simple_write(pa_simple *s,const void *p,size_t n,int *e){(void)s;(void)p;assert(n%4==0);(void)e;return 0;}
int pa_simple_flush(pa_simple *s,int *e){(void)s;(void)e;return 0;}
void pa_simple_free(pa_simple *s){free(s);}
static void pause_ms(int n){struct timespec ts={n/1000,(n%1000)*1000000};nanosleep(&ts,NULL);}
static bool await_receiving(radio_engine *e,bool expected) {
 for(int i=0;i<150;i++){bool active,live;float level;char text[256];engine_snapshot(e,&active,&live,text,sizeof(text),&level);if(live==expected)return true;pause_ms(20);}return false;
}
int main(void) {
 char dir[]="/tmp/n1-engine-test-XXXXXX";assert(mkdtemp(dir));char script[512];snprintf(script,sizeof(script),"%s/rtl_fm",dir);
 FILE *f=fopen(script,"w");assert(f);fputs("#!/usr/bin/python3\nimport os,time,sys,signal\ndef arg(k):return sys.argv[sys.argv.index(k)+1]\nassert arg('-M')=='fm' and arg('-s')=='240000' and arg('-r')=='240000' and arg('-A')=='std'\nwith open(os.environ['N1_FAKE_TUNES'],'a') as f:f.write(arg('-f')+'\\n')\nif os.environ.get('N1_FAKE_BUSY'):\n print('usb_claim_interface error -6',file=__import__('sys').stderr);raise SystemExit(1)\ntime.sleep(float(os.environ.get('N1_FAKE_START_DELAY','0')))\nsignal.signal(signal.SIGINT,lambda *_:sys.exit(0))\nwhile True:\n os.write(1,b'\\x10\\x27'*2048);time.sleep(.02)\n",f);fclose(f);assert(chmod(script,0700)==0);
 const char *old=getenv("PATH");char path[8192];snprintf(path,sizeof(path),"%s:%s",dir,old?old:"");setenv("PATH",path,1);
 char tunes[512];snprintf(tunes,sizeof(tunes),"%s/tunes",dir);setenv("N1_FAKE_TUNES",tunes,1);
 radio_config c;config_defaults(&c);radio_engine e;assert(engine_init(&e,&c));
 engine_apply(&e,&c,true);assert(await_receiving(&e,true));
 pthread_mutex_lock(&e.lock);unsigned gen=e.generation;uint64_t samples=e.audio_samples;pthread_mutex_unlock(&e.lock);assert(samples>0);
 c.volume=17;c.mute=true;engine_apply(&e,&c,true);pause_ms(80);
 pthread_mutex_lock(&e.lock);assert(e.generation==gen);assert(e.audio_samples>samples);pthread_mutex_unlock(&e.lock);
 for(int i=0;i<8;i++){c.hz+=100000;engine_apply(&e,&c,true);pause_ms(120);assert(await_receiving(&e,true));}
 // Rapid alternating +/- updates must converge on the final frequency with
 // a single USB open after the burst, then keep delivering audio.
 FILE *t=fopen(tunes,"r");assert(t);int before=0;char line[80];while(fgets(line,sizeof(line),t))before++;fclose(t);
 for(int i=0;i<40;i++){c.hz+=(i%3?100000:-100000);engine_apply(&e,&c,true);pause_ms(30);}
 assert(await_receiving(&e,true));
 t=fopen(tunes,"r");assert(t);int after=0;long long last=0;while(fgets(line,sizeof(line),t)){after++;last=atoll(line);}fclose(t);
 assert(after==before+1);assert(last==c.hz);
 pthread_mutex_lock(&e.lock);samples=e.audio_samples;pthread_mutex_unlock(&e.lock);pause_ms(150);
 pthread_mutex_lock(&e.lock);assert(e.audio_samples>samples);pthread_mutex_unlock(&e.lock);
 // Cancel a receiver while its USB initialization is still in progress.
 // The worker must let it reach PCM before signaling shutdown, then recover.
 setenv("N1_FAKE_START_DELAY","0.8",1);c.hz+=100000;engine_apply(&e,&c,true);
 pause_ms(550);c.hz+=100000;engine_apply(&e,&c,true);assert(await_receiving(&e,true));
 unsetenv("N1_FAKE_START_DELAY");
 engine_apply(&e,&c,false);assert(await_receiving(&e,false));pause_ms(80);
 setenv("N1_FAKE_BUSY","1",1);engine_apply(&e,&c,true);
 bool saw_busy=false;
 for(int i=0;i<150;i++){bool active,live;float level;char text[256];engine_snapshot(&e,&active,&live,text,sizeof(text),&level);if(strstr(text,"占用")){saw_busy=true;assert(!live);break;}pause_ms(20);}assert(saw_busy);
 pause_ms(100);unsetenv("N1_FAKE_BUSY");engine_apply(&e,&c,true);assert(await_receiving(&e,true));
 engine_destroy(&e);unlink(script);unlink(tunes);rmdir(dir);puts("PASS: receive, volume without retune, 8 retunes, 40 rapid updates coalesced with audio recovery, stop, busy-device failure/retry, shutdown");return 0;
}
