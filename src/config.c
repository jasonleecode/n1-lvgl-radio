#define _POSIX_C_SOURCE 200809L
#include "radio.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

const char *radio_mode_name(radio_mode mode) {
    static const char *names[] = {"FM", "AM", "USB", "LSB", "NFM"};
    return mode>=0 && mode<MODE_COUNT ? names[mode] : "FM";
}
const char *radio_demod(radio_mode mode) {
    static const char *names[] = {"fm", "am", "usb", "lsb", "fm"};
    return mode>=0 && mode<MODE_COUNT ? names[mode] : "wbfm";
}
int radio_sample_rate(radio_mode mode) { return mode==MODE_WFM ? 48000 : 24000; }
void config_defaults(radio_config *c) {
    memset(c,0,sizeof(*c));c->hz=100000000;c->volume=35;c->mode=MODE_WFM;
    c->offset_hz=125000000;c->rf_gain=254;
}
bool radio_validate(const radio_config *c, char *err, unsigned size) {
    const char *s=NULL;
    if(c->hz<RADIO_MIN_HZ || c->hz>RADIO_MAX_HZ) s="频率需在 0.5 至 1766 MHz 之间";
    else if(c->mode<0 || c->mode>=MODE_COUNT || c->input<INPUT_TUNER || c->input>INPUT_UPCONVERTER) s="接收设置无效";
    else if(c->rf_gain< -1 || c->rf_gain>496) s="射频增益超出范围";
    else if(c->volume<0 || c->volume>100 || c->ppm< -200 || c->ppm>200) s="音量或频率校准超出范围";
    else if(c->input==INPUT_TUNER && c->hz<24000000) s="低于 24 MHz 需要直采硬件或上变频器";
    else if(c->input==INPUT_DIRECT_Q && c->hz>28800000) s="直采模式请使用 28.8 MHz 以下频率";
    else if(c->input==INPUT_UPCONVERTER && (c->offset_hz<0 || c->offset_hz>1000000000LL || c->hz+c->offset_hz<24000000 || c->hz+c->offset_hz>RADIO_MAX_HZ)) s="上变频后的硬件频率超出接收范围";
    if(err && size) snprintf(err,size,"%s",s?s:"");
    return s==NULL;
}
static bool integer(const char *s, long long *n) {
    char *end;errno=0;long long v=strtoll(s,&end,10);
    if(errno || end==s || (*end && *end!='\n' && *end!='\r'))return false;
    *n=v;return true;
}
void config_load(radio_config *c, const char *path) {
    config_defaults(c);FILE *f=fopen(path,"r");if(!f)return;
    char line[512];
    while(fgets(line,sizeof(line),f)) {
        char *eq=strchr(line,'=');if(!eq)continue;*eq++=0;
        long long v;
        if(!strcmp(line,"station")) {
            char *a=strchr(eq,',');if(!a)continue;*a++=0;char *b=strchr(a,',');if(!b)continue;*b++=0;
            long long hz,m;if(!integer(eq,&hz)||!integer(a,&m)||hz<RADIO_MIN_HZ||hz>RADIO_MAX_HZ||m<0||m>=MODE_COUNT||c->count>=MAX_STATIONS)continue;
            b[strcspn(b,"\r\n")]=0;station *s=&c->stations[c->count++];s->hz=hz;s->mode=(radio_mode)m;snprintf(s->name,sizeof(s->name),"%.79s",b);continue;
        }
        if(!integer(eq,&v))continue;
        if(!strcmp(line,"frequency") && v>=RADIO_MIN_HZ && v<=RADIO_MAX_HZ)c->hz=v;
        else if(!strcmp(line,"volume") && v>=0 && v<=100)c->volume=(int)v;
        else if(!strcmp(line,"fm_mono") && (v==0 || v==1))c->fm_mono=v;
        else if(!strcmp(line,"mode") && v>=0 && v<MODE_COUNT)c->mode=(radio_mode)v;
        else if(!strcmp(line,"input") && v>=0 && v<=INPUT_UPCONVERTER)c->input=(radio_input)v;
        else if(!strcmp(line,"offset") && v>=0 && v<=1000000000)c->offset_hz=v;
        else if(!strcmp(line,"rf_gain") && v>= -1 && v<=496)c->rf_gain=(int)v;
        else if(!strcmp(line,"ppm") && v>= -200 && v<=200)c->ppm=(int)v;
    }
    fclose(f);
}
bool config_save(const radio_config *c, const char *path) {
    char tmp[4096];if(snprintf(tmp,sizeof(tmp),"%s.tmp.XXXXXX",path)>=(int)sizeof(tmp))return false;
    int fd=mkstemp(tmp);if(fd<0)return false;
    FILE *f=fdopen(fd,"w");if(!f){close(fd);unlink(tmp);return false;}
    fprintf(f,"# N1 Radio v1\nfrequency=%lld\nvolume=%d\nmode=%d\ninput=%d\noffset=%lld\nppm=%d\nrf_gain=%d\n",(long long)c->hz,c->volume,c->mode,c->input,(long long)c->offset_hz,c->ppm,c->rf_gain);
    fprintf(f,"fm_mono=%d\n",c->fm_mono);
    for(int i=0;i<c->count && i<MAX_STATIONS;i++) {
        char name[STATION_NAME];snprintf(name,sizeof(name),"%s",c->stations[i].name);name[strcspn(name,"\r\n")]=0;
        fprintf(f,"station=%lld,%d,%s\n",(long long)c->stations[i].hz,c->stations[i].mode,name);
    }
    bool ok=!ferror(f);if(fflush(f)!=0 || fsync(fd)!=0)ok=false;if(fclose(f)!=0)ok=false;
    if(ok && rename(tmp,path)==0)return true;
    unlink(tmp);return false;
}
