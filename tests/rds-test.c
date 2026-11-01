#define _POSIX_C_SOURCE 200809L
#include "rds.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <time.h>
static void delay(void){struct timespec t={0,10000000};nanosleep(&t,NULL);}
int main(void) {
 rds_info info={.state=RDS_WAITING,.started_ms=1000};
 const char *json="{\"pi\":\"0x1234\",\"ps\":\"  TEST FM \",\"radiotext\":\"Now: \\u97f3\\u4e50\",\"prog_type\":\"News\"}";
 assert(rds_parse_json(&info,json,strlen(json),2000));assert(info.state==RDS_LOCKED);
 assert(!strcmp(info.ps,"TEST FM")&&!strcmp(info.text,"Now: 音乐")&&!strcmp(info.pi,"0x1234")&&info.groups==1);
 const char *same="{\"pi\":\"0x1234\",\"group\":\"1A\"}";assert(rds_parse_json(&info,same,strlen(same),2500));assert(!strcmp(info.ps,"TEST FM"));
 const char *other="{\"pi\":\"0x5678\"}";assert(rds_parse_json(&info,other,strlen(other),3000));assert(!info.ps[0]&&!info.text[0]&&!info.program_type[0]);assert(info.groups==1);
 assert(!rds_parse_json(&info,"{broken",7,4000));assert(!rds_parse_json(&info,"{\"pi\":12}",9,4000));
 const char *garbage="{\"pi\":\"0x1234\"} junk";assert(!rds_parse_json(&info,garbage,strlen(garbage),4000));
 rds_expire(&info,23000);assert(info.state==RDS_STALE);info=(rds_info){.state=RDS_WAITING,.started_ms=1000};rds_expire(&info,11000);assert(info.state==RDS_NO_SIGNAL);
 rds_decoder *d=calloc(1,sizeof(*d));assert(d);setenv("N1_RADIO_REDSEA","/nonexistent/n1-rds-test",1);assert(!rds_decoder_start(d,240000));assert(d->info.state==RDS_UNAVAILABLE);rds_decoder_stop(d);
 char path[]="/tmp/n1-rds-child-XXXXXX";int fd=mkstemp(path);assert(fd>=0);FILE *f=fdopen(fd,"w");assert(f);
 fputs("#!/usr/bin/python3\nimport sys,time,json\nb=sys.stdin.buffer.read(4096)\nassert b==b'\\x10\\x27'*2048\ns=json.dumps({'pi':'0x1234','ps':'TEST FM','radiotext':'Unicode: 音乐'})+'\\n'\nfor part in [s[:8],s[8:20],s[20:]]:\n sys.stdout.write(part);sys.stdout.flush();time.sleep(.02)\nwhile sys.stdin.buffer.read(4096):pass\n",f);fclose(f);assert(chmod(path,0700)==0);setenv("N1_RADIO_REDSEA",path,1);
 assert(rds_decoder_start(d,240000));int16_t raw[2048];for(int i=0;i<2048;i++)raw[i]=10000;
 rds_decoder_push(d,raw,sizeof(raw));
 for(int i=0;i<100 && d->info.state!=RDS_LOCKED;i++){delay();rds_decoder_poll(d);}
 assert(d->info.state==RDS_LOCKED&&!strcmp(d->info.ps,"TEST FM")&&!strcmp(d->info.text,"Unicode: 音乐"));
 rds_decoder_stop(d);assert(d->pid==0&&d->input==-1&&d->output==-1);
 // Early decoder exit must not SIGPIPE the parent or leave descriptors open.
 f=fopen(path,"w");assert(f);fputs("#!/usr/bin/python3\nraise SystemExit(1)\n",f);fclose(f);assert(rds_decoder_start(d,240000));
 for(int i=0;i<100&&d->pid;i++){rds_decoder_push(d,raw,sizeof(raw));delay();}
 assert(d->info.state==RDS_ERROR&&d->pid==0);rds_decoder_stop(d);free(d);unlink(path);unsetenv("N1_RADIO_REDSEA");
 puts("PASS: RDS JSON/Unicode, station reset, missing/malformed data, expiry, fragmented output and decoder failure");return 0;
}
