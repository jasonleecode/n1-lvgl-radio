#define _POSIX_C_SOURCE 200809L
#include "rds.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <string.h>
static void delay(void){struct timespec t={0,9000000};nanosleep(&t,NULL);}
int main(int argc,char **argv) {
 if(argc!=2){fprintf(stderr,"Usage: %s reference-240k-s16le.pcm\n",argv[0]);return 2;}
 FILE *f=fopen(argv[1],"rb");if(!f)return 2;
 rds_decoder *d=calloc(1,sizeof(*d));if(!d||!rds_decoder_start(d,240000))return 2;
 unsigned char data[4096];size_t n;
 while((n=fread(data,1,sizeof(data),f))){rds_decoder_push(d,data,n);delay();}
 fclose(f);
 for(int i=0;i<100 && !d->info.groups;i++){rds_decoder_poll(d);delay();}
 bool ok=d->info.groups && !strcmp(d->info.pi,"0x6201");
 printf("Reference MPX: groups=%llu PI=%s type=%s %s\n",(unsigned long long)d->info.groups,d->info.pi,d->info.program_type,ok?"PASS":"FAIL");
 rds_decoder_stop(d);free(d);return ok?0:1;
}
