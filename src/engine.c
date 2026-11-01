#define _GNU_SOURCE
#include "radio.h"
#include "dsp.h"
#include <pulse/simple.h>
#include <pulse/error.h>
#include <spawn.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/wait.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
extern char **environ;

static void status(radio_engine *e, const char *s, bool live) {
    pthread_mutex_lock(&e->lock);snprintf(e->message,sizeof(e->message),"%s",s);e->receiving=live;
    if(!live)e->audio_level=0;
    pthread_mutex_unlock(&e->lock);
}
static bool changed(radio_engine *e, unsigned gen) {
    pthread_mutex_lock(&e->lock);bool stop=e->quit || !e->active || gen!=e->generation;pthread_mutex_unlock(&e->lock);return stop;
}
static void stop_child(pid_t pid, int audio, int errors) {
    if(pid<=0)return;
    // Keep pipes open and drain them during shutdown. Closing stdout/stderr
    // first can SIGPIPE rtl_fm before it cancels USB transfers and closes SDR.
    kill(pid,SIGINT);
    for(int i=0;i<150;i++) {
        char discard[4096];
        for(int j=0;j<8;j++){if(read(audio,discard,sizeof(discard))<=0)break;}
        for(int j=0;j<8;j++){if(read(errors,discard,sizeof(discard))<=0)break;}
        pid_t result=waitpid(pid,NULL,WNOHANG);
        if(result==pid || (result<0 && errno==ECHILD)){usleep(80000);return;}
        usleep(20000);
    }
    kill(pid,SIGKILL);while(waitpid(pid,NULL,0)<0 && errno==EINTR){}
    usleep(150000);
}
static pid_t start_child(const radio_config *c, int *audio, int *errors) {
    int out[2],err[2];if(pipe2(out,O_CLOEXEC)<0)return -1;
    if(pipe2(err,O_CLOEXEC)<0){close(out[0]);close(out[1]);return -1;}
    char frequency[32],ppm[16],rate[16],gain_arg[16];
    snprintf(frequency,sizeof(frequency),"%lld",(long long)(c->hz+(c->input==INPUT_UPCONVERTER?c->offset_hz:0)));
    snprintf(ppm,sizeof(ppm),"%d",c->ppm);snprintf(rate,sizeof(rate),"%d",c->mode==MODE_WFM?FM_INPUT_RATE:radio_sample_rate(c->mode));
    char *argv[40]={"rtl_fm","-d","0","-M",(char*)radio_demod(c->mode),"-f",frequency,"-p",ppm};int n=9;
    if(c->rf_gain>=0){snprintf(gain_arg,sizeof(gain_arg),"%.1f",c->rf_gain/10.0);argv[n++]="-g";argv[n++]=gain_arg;}
    if(c->mode==MODE_WFM){
        // Explicit wide FM avoids rtl_fm's wbfm alias, which shifts tuning by
        // 16 kHz and defaults to only 170 kHz (insufficient deviation margin).
        argv[n++]="-s";argv[n++]="240000";argv[n++]="-A";argv[n++]="std";
        argv[n++]="-F";argv[n++]="9";
    }
    if(c->mode!=MODE_WFM){argv[n++]="-s";argv[n++]="24000";}
    argv[n++]="-r";argv[n++]=rate;argv[n++]="-E";argv[n++]="dc";
    if(c->input==INPUT_DIRECT_Q){argv[n++]="-E";argv[n++]="direct2";}
    argv[n++]="-";argv[n]=NULL;
    posix_spawn_file_actions_t actions;posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions,out[1],STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions,err[1],STDERR_FILENO);
    posix_spawn_file_actions_addopen(&actions,STDIN_FILENO,"/dev/null",O_RDONLY,0);
    posix_spawn_file_actions_addclose(&actions,out[0]);posix_spawn_file_actions_addclose(&actions,err[0]);
    posix_spawn_file_actions_addclose(&actions,out[1]);posix_spawn_file_actions_addclose(&actions,err[1]);
    pid_t pid;int ret=posix_spawnp(&pid,"rtl_fm",&actions,NULL,argv,environ);
    posix_spawn_file_actions_destroy(&actions);close(out[1]);close(err[1]);
    if(ret){close(out[0]);close(err[0]);errno=ret;return -1;}
    fcntl(out[0],F_SETFL,fcntl(out[0],F_GETFL)|O_NONBLOCK);
    fcntl(err[0],F_SETFL,fcntl(err[0],F_GETFL)|O_NONBLOCK);
    *audio=out[0];*errors=err[0];return pid;
}
static void publish_rds(radio_engine *e,const rds_info *info,unsigned generation) {
    pthread_mutex_lock(&e->lock);
    if(e->generation==generation){
        if(getenv("N1_RADIO_DEBUG") && (e->rds.state!=info->state || strcmp(e->rds.pi,info->pi) || strcmp(e->rds.ps,info->ps) || strlen(e->rds.text)!=strlen(info->text)))
            fprintf(stderr,"rds: state=%d groups=%llu PI=%s PS=%s text_bytes=%zu\n",info->state,(unsigned long long)info->groups,info->pi,info->ps,strlen(info->text));
        e->rds=*info;
    }
    pthread_mutex_unlock(&e->lock);
}
static void receive(radio_engine *e, radio_config *c, unsigned generation) {
    char invalid[256];if(!radio_validate(c,invalid,sizeof(invalid))){status(e,invalid,false);return;}
    status(e,"正在打开接收器…",false);
    int audio=-1,errors=-1;pid_t pid=start_child(c,&audio,&errors);
    if(pid<0){status(e,"无法启动 rtl_fm，请检查程序是否已安装",false);return;}
    pa_sample_spec spec={.format=PA_SAMPLE_S16LE,.rate=(uint32_t)radio_sample_rate(c->mode),.channels=1};
    int error=0;pa_simple *pa=pa_simple_new(NULL,"N1 Radio",PA_STREAM_PLAYBACK,NULL,"Radio audio",&spec,NULL,NULL,&error);
    if(!pa){stop_child(pid,audio,errors);close(audio);close(errors);status(e,"无法连接音频服务，请检查声音输出",false);return;}
    _Alignas(int16_t) unsigned char bytes[4097];size_t carry=0;char log[4096]={0};size_t used=0;bool failed=false,playing=false;fm_resampler fm;fm_resampler_init(&fm);
    rds_decoder *rds=NULL;
    if(c->mode==MODE_WFM){
        fm_resampler_enable_deemphasis(&fm);
        rds=calloc(1,sizeof(*rds));
        if(rds){rds_decoder_start(rds,FM_INPUT_RATE);publish_rds(e,&rds->info,generation);}
        else {rds_info unavailable={.state=RDS_UNAVAILABLE};publish_rds(e,&unavailable,generation);}
    }
    unsigned debug_frames=0;bool diagnostics=getenv("N1_RADIO_DEBUG")!=NULL;
    while(!changed(e,generation)) {
        if(rds){rds_decoder_poll(rds);publish_rds(e,&rds->info,generation);}
        struct pollfd fds[2]={{audio,POLLIN,0},{errors,POLLIN,0}};
        int r=poll(fds,2,100);if(r<0){if(errno==EINTR)continue;failed=true;break;}
        if(fds[1].revents&POLLIN) {
            char buf[512];ssize_t n=read(errors,buf,sizeof(buf)-1);
            if(n>0){buf[n]=0;fprintf(stderr,"rtl_fm: %s",buf);if((size_t)n>sizeof(log)-1-used){used=0;}memcpy(log+used,buf,(size_t)n);used+=(size_t)n;log[used]=0;}
        }
        if(fds[0].revents&(POLLIN|POLLHUP|POLLERR)) {
            ssize_t n=read(audio,bytes+carry,sizeof(bytes)-1-carry);
            if(n==0){failed=true;break;}
            if(n<0){if(errno==EAGAIN||errno==EINTR)continue;failed=true;break;}
            size_t count=(size_t)n+carry,even=count&~(size_t)1;
            bool mute;int volume;
            pthread_mutex_lock(&e->lock);mute=e->config.mute;volume=e->config.volume;pthread_mutex_unlock(&e->lock);
            int16_t raw_samples[2048],processed[2048];memcpy(raw_samples,bytes,even);
            if(rds){rds_decoder_push(rds,raw_samples,even);publish_rds(e,&rds->info,generation);}
            size_t sample_count=even/2;
            carry=count-even;if(carry)bytes[0]=bytes[even];
            if(c->mode==MODE_WFM)sample_count=fm_resampler_process(&fm,raw_samples,sample_count,processed,2048);
            else memcpy(processed,raw_samples,even);
            if(!sample_count)continue;
            int16_t *samples=processed;double sum=0;float gain=mute?0:(float)volume/100;
            for(size_t i=0;i<sample_count;i++){float x=samples[i]/32768.0f;sum+=x*x;samples[i]=(int16_t)(samples[i]*gain);}
            float level=sqrtf((float)(sum/sample_count));
            if(diagnostics && debug_frames++<5)fprintf(stderr,"audio: raw_rms=%.5f gain=%.2f bytes=%zu\n",level,gain,even);
            if(pa_simple_write(pa,processed,sample_count*sizeof(int16_t),&error)<0){snprintf(log,sizeof(log),"audio: %s",pa_strerror(error));failed=true;break;}
            if(changed(e,generation))break;
            pthread_mutex_lock(&e->lock);e->audio_level=level;e->audio_samples+=sample_count;pthread_mutex_unlock(&e->lock);
            if(!playing){status(e,"正在接收",true);playing=true;}
        }
    }
    if(rds){rds_decoder_stop(rds);free(rds);}
    pa_simple_flush(pa,NULL);pa_simple_free(pa);stop_child(pid,audio,errors);close(audio);close(errors);
    if(failed && !changed(e,generation)) {
        if(strstr(log,"-6")||strstr(log,"busy")||strstr(log,"claim"))status(e,"接收器被占用，请先退出 Gqrx 或其他收音机",false);
        else if(strstr(log,"No supported")||strstr(log,"not found")||strstr(log,"-4"))status(e,"未找到接收器，请检查 USB 连接",false);
        else if(strstr(log,"audio:"))status(e,"音频输出中断，请重新开始接收",false);
        else status(e,"接收中断，请检查设备并重新开始",false);
    }
}
static void *worker(void *arg) {
    radio_engine *e=arg;unsigned handled=(unsigned)-1;
    while(true) {
        pthread_mutex_lock(&e->lock);bool quit=e->quit,active=e->active;unsigned gen=e->generation;radio_config c=e->config;pthread_mutex_unlock(&e->lock);
        if(quit)break;
        if(gen!=handled){
            handled=gen;
            if(active){
                receive(e,&c,gen);
                pthread_mutex_lock(&e->lock);
                if(e->generation==gen)e->active=false;
                pthread_mutex_unlock(&e->lock);
            }else status(e,"接收已停止",false);
        }
        usleep(50000);
    }
    return NULL;
}
bool engine_init(radio_engine *e, const radio_config *c) {
    memset(e,0,sizeof(*e));pthread_mutex_init(&e->lock,NULL);e->config=*c;snprintf(e->message,sizeof(e->message),"准备就绪");
    return pthread_create(&e->thread,NULL,worker,e)==0;
}
void engine_apply(radio_engine *e,const radio_config *c,bool active) {
    pthread_mutex_lock(&e->lock);
    bool retune=e->active!=active || e->config.hz!=c->hz || e->config.mode!=c->mode || e->config.input!=c->input || e->config.offset_hz!=c->offset_hz || e->config.ppm!=c->ppm || e->config.rf_gain!=c->rf_gain;
    if(retune){e->generation++;e->receiving=false;e->audio_level=0;memset(&e->rds,0,sizeof(e->rds));e->rds.state=active?(c->mode==MODE_WFM?RDS_WAITING:RDS_NOT_FM):RDS_IDLE;}
    // Permit a retry after a failed start even if tuning parameters did not change.
    if(active && !e->active)e->generation++;
    e->config=*c;e->active=active;pthread_mutex_unlock(&e->lock);
}
void engine_snapshot(radio_engine *e,bool *active,bool *receiving,char *message,unsigned size,float *level) {
    pthread_mutex_lock(&e->lock);*active=e->active;*receiving=e->receiving;*level=e->audio_level;snprintf(message,size,"%s",e->message);pthread_mutex_unlock(&e->lock);
}
void engine_rds_snapshot(radio_engine *e,rds_info *info) {
    pthread_mutex_lock(&e->lock);*info=e->rds;pthread_mutex_unlock(&e->lock);
}
void engine_destroy(radio_engine *e) {
    pthread_mutex_lock(&e->lock);e->quit=true;e->generation++;pthread_mutex_unlock(&e->lock);pthread_join(e->thread,NULL);pthread_mutex_destroy(&e->lock);
}
