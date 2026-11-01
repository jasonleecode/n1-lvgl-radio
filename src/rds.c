#define _GNU_SOURCE
#include "rds.h"
#include <json-c/json.h>
#include <spawn.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <errno.h>
#include <time.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
extern char **environ;

uint64_t rds_now_ms(void) {
    struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);
    return (uint64_t)t.tv_sec*1000+(uint64_t)t.tv_nsec/1000000;
}
static const char *field(struct json_object *obj,const char *key) {
    struct json_object *v=NULL;
    if(!json_object_object_get_ex(obj,key,&v)||!json_object_is_type(v,json_type_string))return NULL;
    return json_object_get_string(v);
}
static void copy_text(char *dst,size_t size,const char *src) {
    if(!src)return;
    size_t original=strlen(src),n=original;if(n>=size)n=size-1;
    if(n<original)while(n && ((unsigned char)src[n]&0xc0)==0x80)n--;
    memcpy(dst,src,n);dst[n]=0;
    for(size_t i=0;i<n;i++)if((unsigned char)dst[i]<32)dst[i]=' ';
    while(n && dst[n-1]==' ')dst[--n]=0;
    size_t first=0;while(dst[first]==' ')first++;
    if(first)memmove(dst,dst+first,n-first+1);
}
bool rds_parse_json(rds_info *info,const char *line,size_t length,uint64_t now) {
    if(!length || length>=RDS_LINE_SIZE)return false;
    struct json_tokener *tok=json_tokener_new();if(!tok)return false;
    json_tokener_set_flags(tok,JSON_TOKENER_STRICT);
    struct json_object *obj=json_tokener_parse_ex(tok,line,(int)length);
    bool valid=json_tokener_get_error(tok)==json_tokener_success && obj && json_object_is_type(obj,json_type_object);
    size_t end=json_tokener_get_parse_end(tok);
    while(end<length && isspace((unsigned char)line[end]))end++;
    if(end!=length)valid=false;
    const char *pi=valid?field(obj,"pi"):NULL;
    if(!pi || strlen(pi)!=6 || strncmp(pi,"0x",2))valid=false;
    if(valid)for(int i=2;i<6;i++)if(!isxdigit((unsigned char)pi[i]))valid=false;
    if(valid) {
        if(strcmp(info->pi,pi)) {
            info->ps[0]=info->text[0]=info->program_type[0]=0;info->groups=0;
        }
        copy_text(info->pi,sizeof(info->pi),pi);
        copy_text(info->ps,sizeof(info->ps),field(obj,"ps"));
        copy_text(info->text,sizeof(info->text),field(obj,"radiotext"));
        copy_text(info->program_type,sizeof(info->program_type),field(obj,"prog_type"));
        info->groups++;info->last_group_ms=now;info->state=RDS_LOCKED;
    }
    if(obj)json_object_put(obj);
    json_tokener_free(tok);return valid;
}
void rds_expire(rds_info *info,uint64_t now) {
    if(info->state==RDS_WAITING && now-info->started_ms>=10000)info->state=RDS_NO_SIGNAL;
    if(info->state==RDS_LOCKED && now-info->last_group_ms>=20000)info->state=RDS_STALE;
}
void rds_decoder_stop(rds_decoder *d) {
    if(d->input>=0){close(d->input);d->input=-1;}
    if(d->pid>0) {
        for(int i=0;i<70;i++) {
            char b[2048];if(d->output>=0)for(int j=0;j<8;j++)if(read(d->output,b,sizeof(b))<=0)break;
            pid_t r=waitpid(d->pid,NULL,WNOHANG);
            if(r==d->pid || (r<0 && errno==ECHILD)){d->pid=0;break;}
            if(i==30)kill(d->pid,SIGTERM);
            usleep(10000);
        }
        if(d->pid>0){kill(d->pid,SIGKILL);while(waitpid(d->pid,NULL,0)<0&&errno==EINTR){}d->pid=0;}
    }
    if(d->output>=0){close(d->output);d->output=-1;}
    d->begin=d->length=0;
}
bool rds_decoder_start(rds_decoder *d,unsigned rate) {
    memset(d,0,sizeof(*d));d->input=d->output=-1;d->info.state=RDS_UNAVAILABLE;
    int input[2],output[2];if(socketpair(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0,input)<0)return false;
    if(pipe2(output,O_CLOEXEC)<0){close(input[0]);close(input[1]);return false;}
    const char *program=getenv("N1_RADIO_REDSEA");
    if(!program || !*program)program=access("/opt/n1-radio/bin/redsea",X_OK)==0?"/opt/n1-radio/bin/redsea":"redsea";
    char samplerate[24];snprintf(samplerate,sizeof(samplerate),"%u",rate);
    char *args[]={(char*)program,"--input","mpx","-r",samplerate,NULL};
    posix_spawn_file_actions_t a;posix_spawn_file_actions_init(&a);
    posix_spawn_file_actions_adddup2(&a,input[0],STDIN_FILENO);posix_spawn_file_actions_adddup2(&a,output[1],STDOUT_FILENO);
    posix_spawn_file_actions_addclose(&a,input[0]);posix_spawn_file_actions_addclose(&a,input[1]);
    posix_spawn_file_actions_addclose(&a,output[0]);posix_spawn_file_actions_addclose(&a,output[1]);
    int ret=posix_spawnp(&d->pid,program,&a,NULL,args,environ);posix_spawn_file_actions_destroy(&a);
    close(input[0]);close(output[1]);
    if(ret){close(input[1]);close(output[0]);d->pid=0;return false;}
    d->input=input[1];d->output=output[0];
    fcntl(d->input,F_SETFL,fcntl(d->input,F_GETFL)|O_NONBLOCK);
    fcntl(d->output,F_SETFL,fcntl(d->output,F_GETFL)|O_NONBLOCK);
    d->info.state=RDS_WAITING;d->info.started_ms=rds_now_ms();return true;
}
static void failed(rds_decoder *d) {
    rds_decoder_stop(d);d->info.state=RDS_ERROR;
}
void rds_decoder_poll(rds_decoder *d) {
    if(!d->pid)return;
    char bytes[2048];
    // Limit work per pass so metadata can never monopolize audio processing.
    for(int pass=0;pass<8;pass++) {
        ssize_t n=read(d->output,bytes,sizeof(bytes));
        if(n==0){failed(d);return;}
        if(n<0){if(errno!=EAGAIN&&errno!=EINTR)failed(d);break;}
        for(ssize_t i=0;i<n;i++) {
            char c=bytes[i];
            if(c=='\n') {
                if(!d->discard_line && d->line_length)rds_parse_json(&d->info,d->line,d->line_length,rds_now_ms());
                d->line_length=0;d->discard_line=false;
            }else if(d->line_length<sizeof(d->line)-1 && !d->discard_line)d->line[d->line_length++]=c;
            else d->discard_line=true;
        }
    }
    while(d->length) {
        ssize_t n=send(d->input,d->pending+d->begin,d->length,MSG_NOSIGNAL);
        if(n<0){if(errno!=EAGAIN && errno!=EINTR)failed(d);break;}
        if(!n)break;
        d->begin+=(size_t)n;d->length-=(size_t)n;
    }
    if(!d->length)d->begin=0;
    rds_expire(&d->info,rds_now_ms());
}
void rds_decoder_push(rds_decoder *d,const void *data,size_t bytes) {
    if(!d->pid)return;
    rds_decoder_poll(d);if(!d->pid)return;
    if(bytes>sizeof(d->pending)-d->length){failed(d);return;}
    if(d->begin+d->length+bytes>sizeof(d->pending)){memmove(d->pending,d->pending+d->begin,d->length);d->begin=0;}
    memcpy(d->pending+d->begin+d->length,data,bytes);d->length+=bytes;
    rds_decoder_poll(d);
}
