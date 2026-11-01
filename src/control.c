#define _GNU_SOURCE
#include "control.h"
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static json_object *reply(bool ok,const char *message) {
 json_object *r=json_object_new_object();json_object_object_add(r,"ok",json_object_new_boolean(ok));
 if(message)json_object_object_add(r,"message",json_object_new_string(message));
 return r;
}
bool control_open(radio_control *s) {
 memset(s,0,sizeof(*s));s->listener=s->lock=-1;for(int i=0;i<8;i++)s->clients[i]=-1;
 char directory[108],lockpath[128],standard[64];const char *runtime=getenv("XDG_RUNTIME_DIR");
 snprintf(standard,sizeof(standard),"/run/user/%lu",(unsigned long)getuid());
 if((!runtime || !*runtime) && access(standard,W_OK)==0)runtime=standard;
 if(runtime && *runtime){if(snprintf(directory,sizeof(directory),"%s/n1-radio",runtime)>=(int)sizeof(directory)){errno=ENAMETOOLONG;return false;}}
 else snprintf(directory,sizeof(directory),"/tmp/n1-radio-%lu",(unsigned long)getuid());
 if(mkdir(directory,0700)<0 && errno!=EEXIST)return false;
 struct stat st;if(lstat(directory,&st)<0 || !S_ISDIR(st.st_mode) || st.st_uid!=getuid() || (st.st_mode&077)){errno=EACCES;return false;}
 if(snprintf(s->path,sizeof(s->path),"%s/control.sock",directory)>=(int)sizeof(s->path)){errno=ENAMETOOLONG;return false;}
 snprintf(lockpath,sizeof(lockpath),"%s/instance.lock",directory);
 s->lock=open(lockpath,O_CREAT|O_RDWR|O_CLOEXEC|O_NOFOLLOW,0600);if(s->lock<0)return false;
 if(flock(s->lock,LOCK_EX|LOCK_NB)<0){close(s->lock);s->lock=-1;return false;}
 s->listener=socket(AF_UNIX,SOCK_SEQPACKET|SOCK_NONBLOCK|SOCK_CLOEXEC,0);if(s->listener<0){control_close(s);return false;}
 struct sockaddr_un addr={.sun_family=AF_UNIX};snprintf(addr.sun_path,sizeof(addr.sun_path),"%s",s->path);unlink(s->path);
 if(bind(s->listener,(struct sockaddr*)&addr,sizeof(addr))<0 || chmod(s->path,0600)<0 || listen(s->listener,8)<0){control_close(s);return false;}return true;
}
void control_close(radio_control *s) {
 for(int i=0;i<8;i++)if(s->clients[i]>=0){close(s->clients[i]);s->clients[i]=-1;}
 if(s->listener>=0){close(s->listener);s->listener=-1;unlink(s->path);}
 if(s->lock>=0){close(s->lock);s->lock=-1;}
}
static json_object *execute(json_object *req,radio_config *c,radio_engine *e,bool *updated,bool *quit) {
 json_object *v;const char *cmd=NULL;
 if(json_object_object_get_ex(req,"command",&v) && json_object_is_type(v,json_type_string))cmd=json_object_get_string(v);
 if(!cmd)return reply(false,"缺少 command");
 bool active,live;float level;char message[256];engine_snapshot(e,&active,&live,message,sizeof(message),&level);
 if(!strcmp(cmd,"status")) {
  json_object *r=reply(true,message);json_object_object_add(r,"frequency_hz",json_object_new_int64(c->hz));json_object_object_add(r,"mode",json_object_new_string(radio_mode_name(c->mode)));
  json_object_object_add(r,"active",json_object_new_boolean(active));json_object_object_add(r,"receiving",json_object_new_boolean(live));json_object_object_add(r,"volume",json_object_new_int(c->volume));json_object_object_add(r,"mute",json_object_new_boolean(c->mute));
  pthread_mutex_lock(&e->lock);bool locked=e->pilot_locked;float blend=e->stereo_blend,pilot=e->pilot_amplitude;pthread_mutex_unlock(&e->lock);
  json_object_object_add(r,"pilot_locked",json_object_new_boolean(locked));json_object_object_add(r,"stereo",json_object_new_boolean(live && c->mode==MODE_WFM && locked && blend>.9f && !c->fm_mono));
  json_object_object_add(r,"stereo_mode",json_object_new_string(c->fm_mono?"mono":"auto"));json_object_object_add(r,"stereo_blend",json_object_new_double(blend));json_object_object_add(r,"pilot_amplitude",json_object_new_double(pilot));json_object_object_add(r,"output_channels",json_object_new_int(c->mode==MODE_WFM?2:1));
  rds_info info;engine_rds_snapshot(e,&info);json_object *rds=json_object_new_object();json_object_object_add(rds,"ps",json_object_new_string(info.ps));json_object_object_add(rds,"text",json_object_new_string(info.text));json_object_object_add(rds,"pi",json_object_new_string(info.pi));json_object_object_add(r,"rds",rds);return r;
 }
 if(!strcmp(cmd,"presets")) {
  json_object *r=reply(true,NULL),*list=json_object_new_array();
  for(int i=0;i<c->count;i++){json_object *p=json_object_new_object();json_object_object_add(p,"number",json_object_new_int(i+1));json_object_object_add(p,"name",json_object_new_string(c->stations[i].name));json_object_object_add(p,"frequency_hz",json_object_new_int64(c->stations[i].hz));json_object_object_add(p,"mode",json_object_new_string(radio_mode_name(c->stations[i].mode)));json_object_array_add(list,p);}json_object_object_add(r,"presets",list);return r;
 }
 radio_config next=*c;bool start=active,validate=false;
 bool has=json_object_object_get_ex(req,"value",&v);
 if(!strcmp(cmd,"start")){start=true;validate=true;}
 else if(!strcmp(cmd,"stop"))start=false;
 else if(!strcmp(cmd,"quit")){*quit=true;return reply(true,"收音机已退出");}
 else if(!strcmp(cmd,"tune") || !strcmp(cmd,"step")) {
  if(!has || !json_object_is_type(v,json_type_int))return reply(false,"频率必须为整数 Hz");
  int64_t n=json_object_get_int64(v);
  if(!strcmp(cmd,"step")){if(n < -RADIO_MAX_HZ || n>RADIO_MAX_HZ)return reply(false,"步进超出范围");next.hz+=n;}else next.hz=n;
  start=true;validate=true;
 }
 else if(!strcmp(cmd,"volume")){if(!has || !json_object_is_type(v,json_type_int) || json_object_get_int64(v)<0 || json_object_get_int64(v)>100)return reply(false,"音量需在 0～100 之间");next.volume=json_object_get_int(v);}
 else if(!strcmp(cmd,"stereo")){
  if(!has || !json_object_is_type(v,json_type_string) || (strcmp(json_object_get_string(v),"auto") && strcmp(json_object_get_string(v),"mono")))return reply(false,"立体声模式需为 auto 或 mono");
  next.fm_mono=!strcmp(json_object_get_string(v),"mono");
 }
 else if(!strcmp(cmd,"mute")){if(!has || !json_object_is_type(v,json_type_boolean))return reply(false,"mute 需要布尔值");next.mute=json_object_get_boolean(v);}
 else if(!strcmp(cmd,"mode")) {
  if(!has || !json_object_is_type(v,json_type_string))return reply(false,"缺少接收模式");
  int m;for(m=0;m<MODE_COUNT;m++)if(!strcmp(json_object_get_string(v),radio_mode_name(m)))break;
  if(m==MODE_COUNT)return reply(false,"模式需为 FM/AM/USB/LSB/NFM");
  next.mode=m;validate=true;
 }
 else if(!strcmp(cmd,"preset")) {
  if(!has || !json_object_is_type(v,json_type_int) || json_object_get_int64(v)<1 || json_object_get_int64(v)>c->count)return reply(false,"收藏编号不存在");
  station *p=&c->stations[json_object_get_int(v)-1];next.hz=p->hz;next.mode=p->mode;start=true;validate=true;
 }else return reply(false,"未知命令");
 // A tune command may select its mode atomically, e.g. tune 7.1M --mode AM.
 if(!strcmp(cmd,"tune") && json_object_object_get_ex(req,"mode",&v)) {
  if(!json_object_is_type(v,json_type_string))return reply(false,"模式无效");
  int m;
  for(m=0;m<MODE_COUNT;m++)if(!strcmp(json_object_get_string(v),radio_mode_name(m)))break;
  if(m==MODE_COUNT)return reply(false,"模式无效");
  next.mode=m;
 }
 char error[256];if(validate && !radio_validate(&next,error,sizeof(error)))return reply(false,error);
 *c=next;engine_apply(e,c,start);*updated=true;return reply(true,"命令已接受");
}
void control_poll(radio_control *s,radio_config *c,radio_engine *e,bool *updated,bool *quit) {
 for(int i=0;i<8;i++)if(s->clients[i]<0){int fd=accept4(s->listener,NULL,NULL,SOCK_NONBLOCK|SOCK_CLOEXEC);if(fd<0)break;s->clients[i]=fd;s->deadlines[i]=rds_now_ms()+2000;}
 for(int i=0;i<8;i++)if(s->clients[i]>=0) {
  char buffer[4096];ssize_t n=recv(s->clients[i],buffer,sizeof(buffer),MSG_DONTWAIT|MSG_TRUNC);
  if(n<0 && (errno==EAGAIN || errno==EWOULDBLOCK) && rds_now_ms()<s->deadlines[i])continue;
  if(n>0){json_object *req=NULL,*response;json_tokener *tok=json_tokener_new();
   if(n<(ssize_t)sizeof(buffer)){req=json_tokener_parse_ex(tok,buffer,(int)n);if(json_tokener_get_error(tok)!=json_tokener_success || json_tokener_get_parse_end(tok)!=(size_t)n){if(req)json_object_put(req);req=NULL;}}
   response=req && json_object_is_type(req,json_type_object)?execute(req,c,e,updated,quit):reply(false,"无效或过长的 JSON 请求");
   const char *out=json_object_to_json_string_ext(response,JSON_C_TO_STRING_PLAIN);send(s->clients[i],out,strlen(out),MSG_NOSIGNAL|MSG_DONTWAIT);
   json_object_put(response);if(req)json_object_put(req);json_tokener_free(tok);
  }
  close(s->clients[i]);s->clients[i]=-1;
 }
}
