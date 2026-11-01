#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L
#include "radio.h"
#include "control.h"
#include <lvgl/lvgl.h>
#include <SDL2/SDL.h>
#include <lvgl/drivers/sdl/lv_sdl_window.h>
#include <lvgl/drivers/sdl/lv_sdl_mouse.h>
#include <lvgl/drivers/sdl/lv_sdl_mousewheel.h>
#include <lvgl/drivers/sdl/lv_sdl_keyboard.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>

#define WINDOW_WIDTH 1040
#define WINDOW_HEIGHT 800

LV_FONT_DECLARE(radio_font_20);
LV_FONT_DECLARE(radio_font_76);
static const unsigned BG=0x0D1820,CARD=0x15252F,INNER=0x1B303B,INK=0xEAF1EE,MUTED=0x96ADB4,TEAL=0x64D7C2,LINE=0x2B414C,RED=0xFFB0A3;
static radio_config cfg;
static radio_engine engine;
static bool initialized=false,active=false,demo=false,cleaned=false;
static volatile sig_atomic_t exiting=0;
static bool headless=false,control_ready=false;
static radio_control control;
static char config_path[4096],notice[256];static uint32_t notice_until=0;
static lv_obj_t *root,*freq_label,*mode_select,*step_select,*status_label,*clock_label,*play_label,*play_button,*mute_label,*volume_slider,*volume_label,*meter,*meter_label,*hardware_label,*preset_list,*band_label,*input_label,*rds_name_label,*rds_text_label,*rds_meta_label;
static lv_obj_t *stereo_label,*stereo_button_label,*stereo_button;
static lv_obj_t *overlay,*modal,*edit,*modal_error,*name_edit,*input_select,*offset_edit,*ppm_edit,*gain_select;
static int current_step=1;
static const int gains[]={-1,0,125,197,254,280,328,372,402,439,496};
static int64_t steps[]={100,1000,10000,100000,1000000};
static const char *band="FM 广播";
static bool ui_play_state=false;
static bool save_pending=false;
static uint64_t save_due=0;

static lv_obj_t *label(lv_obj_t *parent,const char *text,int x,int y,unsigned color) {
    lv_obj_t *o=lv_label_create(parent);lv_obj_set_style_text_font(o,&radio_font_20,0);lv_label_set_text(o,text);lv_obj_set_pos(o,x,y);lv_obj_set_style_text_color(o,lv_color_hex(color),0);return o;
}
static lv_obj_t *panel(lv_obj_t *parent,int x,int y,int w,int h,unsigned color) {
    lv_obj_t *o=lv_obj_create(parent);lv_obj_remove_style_all(o);lv_obj_set_pos(o,x,y);lv_obj_set_size(o,w,h);
    lv_obj_set_style_bg_color(o,lv_color_hex(color),0);lv_obj_set_style_bg_opa(o,LV_OPA_COVER,0);lv_obj_set_style_radius(o,18,0);lv_obj_set_scrollable(o,false);return o;
}
static lv_obj_t *button(lv_obj_t *parent,const char *text,int x,int y,int w,int h,bool primary,lv_event_cb_t cb,void *data) {
    lv_obj_t *o=lv_button_create(parent);lv_obj_remove_style_all(o);lv_obj_set_pos(o,x,y);lv_obj_set_size(o,w,h);
    lv_obj_set_style_bg_color(o,lv_color_hex(primary?TEAL:INNER),0);lv_obj_set_style_bg_opa(o,LV_OPA_COVER,0);lv_obj_set_style_radius(o,12,0);
    lv_obj_set_style_border_width(o,primary?0:1,0);lv_obj_set_style_border_color(o,lv_color_hex(LINE),0);
    lv_obj_set_style_bg_color(o,lv_color_hex(primary?0x91ECDB:0x304B59),LV_STATE_PRESSED);
    lv_obj_set_style_outline_color(o,lv_color_hex(TEAL),LV_STATE_FOCUSED);lv_obj_set_style_outline_width(o,2,LV_STATE_FOCUSED);
    lv_obj_t *l=label(o,text,0,0,primary?BG:INK);lv_obj_center(l);
    if(cb)lv_obj_add_event_cb(o,cb,LV_EVENT_CLICKED,data);
    return o;
}
static void dropdown_style(lv_obj_t *o) {
    lv_obj_set_style_text_font(o,&radio_font_20,0);
    lv_obj_set_style_bg_color(o,lv_color_hex(INNER),0);
    lv_obj_set_style_text_color(o,lv_color_hex(INK),0);
    lv_obj_set_style_border_color(o,lv_color_hex(LINE),0);
    lv_obj_set_style_border_width(o,1,0);
    lv_obj_set_style_radius(o,12,0);
    lv_obj_t *list=lv_dropdown_get_list(o);
    if(list){
        lv_obj_set_style_bg_color(list,lv_color_hex(INNER),0);
        lv_obj_set_style_text_color(list,lv_color_hex(INK),0);
        lv_obj_set_style_text_font(list,&radio_font_20,0);
        lv_obj_set_style_bg_color(list,lv_color_hex(TEAL),LV_PART_SELECTED);
        lv_obj_set_style_text_font(list,&radio_font_20,LV_PART_SELECTED);
        lv_obj_set_style_text_color(list,lv_color_hex(BG),LV_PART_SELECTED);
    }
}
static void tell(const char *s) {snprintf(notice,sizeof(notice),"%s",s);notice_until=lv_tick_get()+6500;}
static void save(void) {if(!demo && !config_save(&cfg,config_path)){if(headless)fputs("设置保存失败\n",stderr);else tell("设置保存失败，请检查磁盘空间");}}
static void refresh_presets(void);
static void refresh_tuning(void) {
    char text[128];snprintf(text,sizeof(text),"%.4f",cfg.hz/1000000.0);lv_label_set_text(freq_label,text);
    lv_dropdown_set_selected(mode_select,cfg.mode);
    lv_label_set_text(band_label,band);
    if(step_select)lv_dropdown_set_selected(step_select,current_step);
    const char *input=cfg.input==INPUT_TUNER?"常规调谐":cfg.input==INPUT_DIRECT_Q?"直采 Q 输入":"上变频输入";
    lv_label_set_text(input_label,input);
    char validation[256];bool valid=radio_validate(&cfg,validation,sizeof(validation));
    lv_label_set_text(hardware_label,valid?"RTL-SDR  /  默认音频输出":(cfg.hz<24000000 && cfg.input==INPUT_TUNER?"低频需直采或上变频硬件":"请检查接收硬件设置"));
    lv_obj_set_style_text_color(hardware_label,lv_color_hex(valid?MUTED:RED),0);
}
static void apply(void) {
    char error[256];if(active && !radio_validate(&cfg,error,sizeof(error))){active=false;tell(error);}
    if(!demo)engine_apply(&engine,&cfg,active);
    refresh_tuning();save_pending=true;save_due=rds_now_ms()+750;
}
static void close_modal(void) {if(overlay){lv_obj_delete(overlay);overlay=NULL;modal=NULL;edit=NULL;}}
static void close_event(lv_event_t *e){(void)e;close_modal();}
static void make_modal(const char *title,int w,int h) {
    close_modal();overlay=panel(lv_layer_top(),0,0,WINDOW_WIDTH,WINDOW_HEIGHT,BG);lv_obj_set_style_text_font(overlay,&radio_font_20,0);lv_obj_set_style_bg_opa(overlay,220,0);lv_obj_set_style_radius(overlay,0,0);
    modal=panel(overlay,(WINDOW_WIDTH-w)/2,(WINDOW_HEIGHT-h)/2,w,h,CARD);label(modal,title,22,20,INK);
    button(modal,"关闭",w-94,12,74,42,false,close_event,NULL);
    modal_error=label(modal,"",22,h-42,RED);lv_obj_set_width(modal_error,w-44);
}
static const char *infer_band(void) {
    if(cfg.hz>=87500000 && cfg.hz<=108000000 && cfg.mode==MODE_WFM)return "FM 广播";
    if(cfg.hz<1800000)return "中波 AM";
    if(cfg.hz<30000000)return "短波";
    return "手动调谐";
}
static void tune_frequency(int64_t hz) {cfg.hz=hz;band=infer_band();apply();}
static void frequency_commit(lv_event_t *e) {
    (void)e;const char *s=lv_textarea_get_text(edit);char *end;errno=0;double value=strtod(s,&end);
    if(errno||end==s||*end||!isfinite(value)||value<.5||value>1766){lv_label_set_text(modal_error,"请输入 0.5 至 1766 之间的 MHz 频率");return;}
    int64_t hz=llround(value*1000000.0);close_modal();tune_frequency(hz);
}
static void show_frequency(lv_event_t *e) {
    (void)e;make_modal("输入频率  /  MHz",620,540);edit=lv_textarea_create(modal);lv_obj_set_style_text_font(edit,&radio_font_20,0);lv_obj_set_pos(edit,22,82);lv_obj_set_size(edit,576,54);
    lv_textarea_set_one_line(edit,true);lv_textarea_set_accepted_chars(edit,"0123456789.");lv_textarea_set_max_length(edit,12);
    char text[32];snprintf(text,sizeof(text),"%.4f",cfg.hz/1000000.0);lv_textarea_set_text(edit,text);
    lv_obj_t *kb=lv_keyboard_create(modal);lv_obj_set_align(kb,LV_ALIGN_TOP_LEFT);lv_obj_set_style_text_font(kb,&radio_font_20,0);lv_obj_set_style_text_font(kb,&radio_font_20,LV_PART_ITEMS);lv_obj_set_pos(kb,14,210);lv_obj_set_size(kb,592,260);lv_keyboard_set_mode(kb,LV_KEYBOARD_MODE_NUMBER);lv_keyboard_set_textarea(kb,edit);
    lv_obj_add_event_cb(kb,frequency_commit,LV_EVENT_READY,NULL);lv_obj_add_event_cb(kb,close_event,LV_EVENT_CANCEL,NULL);
    button(modal,"调到此频率",22,150,220,44,true,frequency_commit,NULL);
    lv_obj_add_event_cb(edit,frequency_commit,LV_EVENT_READY,NULL);
    lv_group_focus_obj(edit);
}
static void step_click(lv_event_t *e) {
    intptr_t direction=(intptr_t)lv_event_get_user_data(e);int64_t hz=cfg.hz+direction*steps[current_step];
    if(hz<RADIO_MIN_HZ||hz>RADIO_MAX_HZ){tell("已到接收频率边界");return;}tune_frequency(hz);
}
static void step_change(lv_event_t *e){current_step=lv_dropdown_get_selected(lv_event_get_target(e));}
static void mode_change(lv_event_t *e){cfg.mode=(radio_mode)lv_dropdown_get_selected(lv_event_get_target(e));band=infer_band();apply();}
static void band_click(lv_event_t *e) {
    intptr_t i=(intptr_t)lv_event_get_user_data(e);
    if(i==0){band="FM 广播";cfg.hz=100000000;cfg.mode=MODE_WFM;current_step=3;}
    else if(i==1){band="中波 AM";cfg.hz=1000000;cfg.mode=MODE_AM;current_step=1;}
    else if(i==2){band="短波";cfg.hz=7100000;cfg.mode=MODE_AM;current_step=1;}
    else{band="VHF";cfg.hz=118000000;cfg.mode=MODE_AM;current_step=2;}
    apply();
}
static void play_click(lv_event_t *e) {
    (void)e;char error[256];if(!active && !radio_validate(&cfg,error,sizeof(error))){tell(error);return;}
    active=!active;if(!demo)engine_apply(&engine,&cfg,active);
}
static void stereo_click(lv_event_t *e){(void)e;cfg.fm_mono=!cfg.fm_mono;apply();}
static void mute_click(lv_event_t *e){(void)e;cfg.mute=!cfg.mute;apply();lv_label_set_text(mute_label,cfg.mute?"取消静音":"静音");}
static void volume_change(lv_event_t *e) {
    cfg.volume=(int)lv_slider_get_value(lv_event_get_target(e));char text[32];snprintf(text,sizeof(text),"%d%%",cfg.volume);lv_label_set_text(volume_label,text);
    if(!demo)engine_apply(&engine,&cfg,active);
}
static void volume_save(lv_event_t *e){(void)e;save();}
static void preset_pick(lv_event_t *e) {
    int i=(int)(intptr_t)lv_event_get_user_data(e);if(i<0||i>=cfg.count)return;
    cfg.hz=cfg.stations[i].hz;cfg.mode=cfg.stations[i].mode;band="收藏电台";apply();
}
static void preset_delete(lv_event_t *e) {
    int i=(int)(intptr_t)lv_event_get_user_data(e);if(i<0||i>=cfg.count)return;
    for(int j=i;j<cfg.count-1;j++)cfg.stations[j]=cfg.stations[j+1];
    cfg.count--;save();refresh_presets();tell("已移除收藏");
}
static void preset_commit(lv_event_t *e) {
    (void)e;if(cfg.count>=MAX_STATIONS){lv_label_set_text(modal_error,"最多保存 16 个电台，请先移除一个");return;}
    const char *text=lv_textarea_get_text(name_edit);if(!*text){lv_label_set_text(modal_error,"请输入电台名称");return;}
    station *s=&cfg.stations[cfg.count++];s->hz=cfg.hz;s->mode=cfg.mode;snprintf(s->name,sizeof(s->name),"%s",text);close_modal();save();refresh_presets();tell("已收藏当前电台");
}
static void show_preset(lv_event_t *e) {
    (void)e;if(cfg.count>=MAX_STATIONS){tell("收藏已满：最多 16 个电台");return;}
    make_modal("收藏当前电台",620,540);name_edit=lv_textarea_create(modal);lv_obj_set_style_text_font(name_edit,&radio_font_20,0);lv_obj_set_pos(name_edit,22,82);lv_obj_set_size(name_edit,576,54);lv_textarea_set_one_line(name_edit,true);lv_textarea_set_max_length(name_edit,24);
    char text[80];snprintf(text,sizeof(text),"电台 %02d",cfg.count+1);lv_textarea_set_text(name_edit,text);
    button(modal,"保存电台",22,150,200,44,true,preset_commit,NULL);
    lv_obj_t *kb=lv_keyboard_create(modal);lv_obj_set_align(kb,LV_ALIGN_TOP_LEFT);lv_obj_set_style_text_font(kb,&radio_font_20,0);lv_obj_set_style_text_font(kb,&radio_font_20,LV_PART_ITEMS);lv_obj_set_pos(kb,14,210);lv_obj_set_size(kb,592,260);lv_keyboard_set_textarea(kb,name_edit);
    lv_obj_add_event_cb(kb,preset_commit,LV_EVENT_READY,NULL);lv_obj_add_event_cb(kb,close_event,LV_EVENT_CANCEL,NULL);lv_group_focus_obj(name_edit);
}
static void refresh_presets(void) {
    lv_obj_clean(preset_list);
    if(!cfg.count){lv_obj_t *l=label(preset_list,"还没有收藏电台\n\n调到喜欢的频率后\n点击“收藏当前”",10,28,MUTED);lv_obj_set_width(l,195);return;}
    for(int i=0;i<cfg.count;i++) {
        lv_obj_t *b=button(preset_list,"",0,i*86,210,76,false,preset_pick,(void*)(intptr_t)i);
        lv_obj_t *name=label(b,cfg.stations[i].name,10,9,INK);lv_obj_set_width(name,145);lv_label_set_long_mode(name,LV_LABEL_LONG_DOT);
        char t[64];snprintf(t,sizeof(t),"%.3f %s",cfg.stations[i].hz/1000000.0,radio_mode_name(cfg.stations[i].mode));label(b,t,10,40,MUTED);
        button(b,"x",169,18,32,34,false,preset_delete,(void*)(intptr_t)i);
    }
}
static void hardware_commit(lv_event_t *e) {
    (void)e;char *end;errno=0;double mhz=strtod(lv_textarea_get_text(offset_edit),&end);
    if(errno||*end||!isfinite(mhz)||mhz<0||mhz>1000){lv_label_set_text(modal_error,"频移请输入 0 至 1000 MHz");return;}
    char *p_end;long ppm=strtol(lv_textarea_get_text(ppm_edit),&p_end,10);
    if(*p_end||ppm< -200||ppm>200){lv_label_set_text(modal_error,"校准请输入 -200 至 200 ppm");return;}
    cfg.input=(radio_input)lv_dropdown_get_selected(input_select);cfg.offset_hz=llround(mhz*1000000.0);cfg.ppm=(int)ppm;cfg.rf_gain=gains[lv_dropdown_get_selected(gain_select)];close_modal();apply();
}
static void show_hardware(lv_event_t *e) {
    (void)e;make_modal("接收硬件设置",700,620);
    label(modal,"输入方式",24,84,MUTED);input_select=lv_dropdown_create(modal);lv_obj_set_pos(input_select,210,74);lv_obj_set_size(input_select,456,50);
    lv_dropdown_set_options(input_select,"常规调谐（约 24 MHz 起）\n直采 Q（需要硬件支持）\n上变频器（设置频移）");lv_dropdown_set_selected(input_select,cfg.input);dropdown_style(input_select);
    label(modal,"上变频频移 MHz",24,158,MUTED);offset_edit=lv_textarea_create(modal);lv_obj_set_style_text_font(offset_edit,&radio_font_20,0);lv_obj_set_pos(offset_edit,270,144);lv_obj_set_size(offset_edit,396,50);lv_textarea_set_one_line(offset_edit,true);lv_textarea_set_accepted_chars(offset_edit,"0123456789.");
    char t[32];snprintf(t,sizeof(t),"%.3f",cfg.offset_hz/1000000.0);lv_textarea_set_text(offset_edit,t);
    label(modal,"频率校准 ppm",24,232,MUTED);ppm_edit=lv_textarea_create(modal);lv_obj_set_style_text_font(ppm_edit,&radio_font_20,0);lv_obj_set_pos(ppm_edit,270,218);lv_obj_set_size(ppm_edit,396,50);lv_textarea_set_one_line(ppm_edit,true);lv_textarea_set_accepted_chars(ppm_edit,"-0123456789");snprintf(t,sizeof(t),"%d",cfg.ppm);lv_textarea_set_text(ppm_edit,t);
    label(modal,"射频增益",24,308,MUTED);gain_select=lv_dropdown_create(modal);lv_obj_set_pos(gain_select,270,294);lv_obj_set_size(gain_select,396,50);
    lv_dropdown_set_options(gain_select,"自动增益\n0 dB\n12.5 dB\n19.7 dB\n25.4 dB\n28.0 dB\n32.8 dB\n37.2 dB\n40.2 dB\n43.9 dB\n49.6 dB");
    for(unsigned i=0;i<sizeof(gains)/sizeof(gains[0]);i++)if(gains[i]==cfg.rf_gain)lv_dropdown_set_selected(gain_select,i);
    dropdown_style(gain_select);
    lv_obj_t *help=label(modal,"普通 R820T 接收棒不能仅靠软件接收低频短波。\n直采需要硬件支持；上变频频移请按设备规格填写。\n直采模式建议用于 28.8 MHz 以下。\n增益过高可能失真，可先尝试 25.4 dB。",24,374,MUTED);lv_obj_set_width(help,646);
    button(modal,"保存设置",24,510,200,48,true,hardware_commit,NULL);
}
static void label_changed(lv_obj_t *o,const char *text) {
    if(strcmp(lv_label_get_text(o),text))lv_label_set_text(o,text);
}
static void update_rds(void) {
    rds_info info={0};if(!demo)engine_rds_snapshot(&engine,&info);
    if(cfg.mode!=MODE_WFM)info.state=RDS_NOT_FM;
    else if(!active)info.state=RDS_IDLE;
    const char *name="RDS 待机",*text="开始 FM 接收后自动检测电台信息";
    char meta[320]="";
    switch(info.state) {
    case RDS_NOT_FM:name="RDS 仅用于 FM 广播";text="当前接收模式不携带 RDS 信息";break;
    case RDS_WAITING:name="正在检测 RDS…";text="等待电台发送名称和节目文字";break;
    case RDS_NO_SIGNAL:name="未检测到 RDS";text="电台可能未发送数据，或当前信号较弱";break;
    case RDS_LOCKED:
    case RDS_STALE:
        name=info.ps[0]?info.ps:"RDS 已锁定";
        text=info.text[0]?info.text:"正在等待节目文字";
        snprintf(meta,sizeof(meta),"PI %s  ·  %s  ·  %s",info.pi,info.program_type[0]?info.program_type:"节目类型待接收",info.state==RDS_STALE?"RDS 信号暂失":"RDS 数据正常");break;
    case RDS_UNAVAILABLE:name="RDS 解码器不可用";text="可以继续收听广播";break;
    case RDS_ERROR:name="RDS 解码中断";text="请重新开始接收以重试；广播音频可继续播放";break;
    default:break;
    }
    label_changed(rds_name_label,name);label_changed(rds_text_label,text);label_changed(rds_meta_label,meta);
    static unsigned old_color=0;unsigned color=info.state==RDS_LOCKED?TEAL:INK;
    if(color!=old_color){lv_obj_set_style_text_color(rds_name_label,lv_color_hex(color),0);old_color=color;}
}
static void update(lv_timer_t *timer) {
    if(save_pending && rds_now_ms()>=save_due){save_pending=false;save();}
    (void)timer;bool receiving=false;float level=0;char message[256]="准备就绪";
    if(!demo)engine_snapshot(&engine,&active,&receiving,message,sizeof(message),&level);
    if(demo && active){receiving=true;level=.08f+.06f*sinf(lv_tick_get()/600.0f);snprintf(message,sizeof(message),"界面预览，不连接接收器");}
    if(ui_play_state!=active || !lv_label_get_text(play_label)[0]) {
        ui_play_state=active;lv_label_set_text(play_label,active?"停止接收":"开始接收");lv_obj_center(play_label);
    }
    update_rds();
    bool locked=false;float blend=0;
    if(!demo){pthread_mutex_lock(&engine.lock);locked=engine.pilot_locked;blend=engine.stereo_blend;pthread_mutex_unlock(&engine.lock);}
    const char *stereo=cfg.mode!=MODE_WFM?"MONO":(!receiving?"FM · --":(locked && blend>.9f && !cfg.fm_mono?"FM STEREO · 19kHz LOCK":"FM MONO"));
    label_changed(stereo_label,stereo);label_changed(stereo_button_label,cfg.fm_mono?"MONO":"AUTO");
    lv_obj_set_style_text_color(stereo_label,lv_color_hex(locked && blend>.9f && !cfg.fm_mono?TEAL:MUTED),0);
    if(cfg.mode==MODE_WFM)lv_obj_remove_state(stereo_button,LV_STATE_DISABLED);else lv_obj_add_state(stereo_button,LV_STATE_DISABLED);
    char s[256];snprintf(s,sizeof(s),"%s",(int32_t)(notice_until-lv_tick_get())>0?notice:message);
    if(strcmp(lv_label_get_text(status_label),s))lv_label_set_text(status_label,s);
    float db=level>0?20*log10f(level):-90;int amount=(int)((db+60)*100/60);if(amount<0)amount=0;if(amount>100)amount=100;
    lv_bar_set_value(meter,amount,LV_ANIM_OFF);snprintf(s,sizeof(s),"音频电平  %s",receiving?"":"--");if(receiving)snprintf(s,sizeof(s),"音频电平  %.0f dBFS",db);
    if(strcmp(lv_label_get_text(meter_label),s))lv_label_set_text(meter_label,s);
    time_t now=time(NULL);struct tm local;localtime_r(&now,&local);strftime(s,sizeof(s),"%H:%M",&local);
    if(strcmp(lv_label_get_text(clock_label),s))lv_label_set_text(clock_label,s);
}
static void create_ui(void) {
    root=lv_screen_active();lv_obj_set_style_bg_color(root,lv_color_hex(BG),0);lv_obj_set_style_text_font(root,&radio_font_20,0);lv_obj_set_style_text_color(root,lv_color_hex(INK),0);lv_obj_set_scrollable(root,false);
    label(root,"N1 收音机",24,24,INK);label(root,"RTL-SDR   /   LVGL",200,27,MUTED);clock_label=label(root,"",938,26,MUTED);
    button(root,"接收设置",774,17,140,45,false,show_hardware,NULL);
    lv_obj_t *side=panel(root,24,88,242,652,CARD);label(side,"收藏电台",16,18,INK);label(side,"PRESETS",16,50,MUTED);
    button(side,"+ 收藏当前",14,95,214,44,false,show_preset,NULL);
    preset_list=panel(side,16,156,214,480,CARD);lv_obj_set_scrollable(preset_list,true);lv_obj_set_scroll_dir(preset_list,LV_DIR_VER);
    lv_obj_t *tuner=panel(root,286,88,730,340,CARD);
    band_label=label(tuner,band,24,20,TEAL);input_label=label(tuner,"",450,20,MUTED);
    lv_obj_t *frequency=button(tuner,"",24,65,548,112,false,show_frequency,NULL);lv_obj_set_style_bg_color(frequency,lv_color_hex(CARD),0);lv_obj_set_style_border_width(frequency,0,0);
    freq_label=label(frequency,"",0,0,INK);lv_obj_set_style_text_font(freq_label,&radio_font_76,0);lv_obj_align(freq_label,LV_ALIGN_LEFT_MID,0,0);label(tuner,"MHz",604,111,MUTED);
    button(tuner,"-",24,205,72,58,false,step_click,(void*)(intptr_t)-1);
    button(tuner,"+",108,205,72,58,false,step_click,(void*)(intptr_t)1);
    lv_obj_t *step=lv_dropdown_create(tuner);step_select=step;lv_obj_set_pos(step,201,207);lv_obj_set_size(step,164,54);lv_dropdown_set_options(step,"100 Hz\n1 kHz\n10 kHz\n100 kHz\n1 MHz");current_step=cfg.mode==MODE_WFM?3:1;lv_dropdown_set_selected(step,current_step);lv_obj_add_event_cb(step,step_change,LV_EVENT_VALUE_CHANGED,NULL);dropdown_style(step);
    mode_select=lv_dropdown_create(tuner);lv_obj_set_pos(mode_select,386,207);lv_obj_set_size(mode_select,142,54);lv_dropdown_set_options(mode_select,"FM\nAM\nUSB\nLSB\nNFM");lv_obj_add_event_cb(mode_select,mode_change,LV_EVENT_VALUE_CHANGED,NULL);dropdown_style(mode_select);
    stereo_label=label(tuner,"FM · --",24,291,MUTED);label(tuner,"模式",548,221,MUTED);
    lv_obj_t *rds_panel=panel(root,286,444,730,112,CARD);
    label(rds_panel,"RDS",18,12,TEAL);rds_name_label=label(rds_panel,"RDS 待机",84,12,INK);lv_obj_set_width(rds_name_label,622);lv_label_set_long_mode(rds_name_label,LV_LABEL_LONG_DOT);
    rds_text_label=label(rds_panel,"开始 FM 接收后自动检测电台信息",18,47,MUTED);lv_obj_set_size(rds_text_label,694,28);lv_label_set_long_mode(rds_text_label,LV_LABEL_LONG_SCROLL_CIRCULAR);
    rds_meta_label=label(rds_panel,"",18,82,MUTED);lv_obj_set_width(rds_meta_label,694);lv_label_set_long_mode(rds_meta_label,LV_LABEL_LONG_DOT);
    lv_obj_t *bands=panel(root,286,572,730,62,BG);const char *bands_text[]={"FM 广播","中波 AM","短波","VHF"};
    for(int i=0;i<4;i++)button(bands,bands_text[i],i*187,0,169,52,false,band_click,(void*)(intptr_t)i);
    lv_obj_t *audio=panel(root,286,640,730,100,CARD);
    play_button=button(audio,"",18,22,166,54,true,play_click,NULL);play_label=lv_obj_get_child(play_button,0);
    lv_obj_t *mute=button(audio,"静音",200,22,110,54,false,mute_click,NULL);mute_label=lv_obj_get_child(mute,0);
    volume_slider=lv_slider_create(audio);lv_obj_set_pos(volume_slider,340,37);lv_obj_set_size(volume_slider,174,16);lv_slider_set_range(volume_slider,0,100);lv_slider_set_value(volume_slider,cfg.volume,LV_ANIM_OFF);lv_obj_set_style_bg_color(volume_slider,lv_color_hex(TEAL),LV_PART_INDICATOR);lv_obj_set_style_bg_color(volume_slider,lv_color_hex(TEAL),LV_PART_KNOB);lv_obj_add_event_cb(volume_slider,volume_change,LV_EVENT_VALUE_CHANGED,NULL);lv_obj_add_event_cb(volume_slider,volume_save,LV_EVENT_RELEASED,NULL);
    char text[24];snprintf(text,sizeof(text),"%d%%",cfg.volume);volume_label=label(audio,text,540,31,INK);
    stereo_button=button(audio,"AUTO",620,22,94,54,false,stereo_click,NULL);stereo_button_label=lv_obj_get_child(stereo_button,0);
    hardware_label=label(root,"",24,760,MUTED);lv_obj_set_width(hardware_label,430);
    status_label=label(root,"准备就绪",478,760,TEAL);lv_obj_set_width(status_label,538);lv_label_set_long_mode(status_label,LV_LABEL_LONG_DOT);
    // Audio level is intentionally not labelled RF signal strength.
    meter=lv_bar_create(tuner);lv_obj_set_pos(meter,550,281);lv_obj_set_size(meter,152,8);lv_bar_set_range(meter,0,100);lv_obj_set_style_bg_color(meter,lv_color_hex(TEAL),LV_PART_INDICATOR);
    meter_label=label(tuner,"音频电平  --",476,301,MUTED);
    refresh_tuning();refresh_presets();lv_timer_create(update,100,NULL);
}
static void poll_control(void) {
    if(!control_ready)return;
    radio_config before=cfg;
    bool updated=false,quit=false;control_poll(&control,&cfg,&engine,&updated,&quit);
    if(updated){
        bool live;float level;char message[256];engine_snapshot(&engine,&active,&live,message,sizeof(message),&level);
        if(!headless){band=infer_band();refresh_tuning();lv_slider_set_value(volume_slider,cfg.volume,LV_ANIM_OFF);char value[16];snprintf(value,sizeof(value),"%d%%",cfg.volume);lv_label_set_text(volume_label,value);lv_label_set_text(mute_label,cfg.mute?"取消静音":"静音");}
        bool presets_changed=before.count!=cfg.count || memcmp(before.stations,cfg.stations,sizeof(cfg.stations));
        if(presets_changed){if(!headless)refresh_presets();save();}
        else {save_pending=true;save_due=rds_now_ms()+750;}
    }
    if(quit)exiting=1;
}
static void cleanup(void) {if(cleaned)return;cleaned=true;if(initialized){engine_destroy(&engine);initialized=false;}save();if(control_ready){control_close(&control);control_ready=false;}}
static void signal_exit(int sig){(void)sig;exiting=1;}
int main(int argc,char **argv) {
    int exit_after=0;const char *preview_popup=NULL;bool start_receiving=false;for(int i=1;i<argc;i++) {
        if(!strcmp(argv[i],"--headless"))headless=true;
        else if(!strcmp(argv[i],"--demo"))demo=true;
        else if(!strcmp(argv[i],"--start"))start_receiving=true;
        else if(!strcmp(argv[i],"--preview-popup")&&i+1<argc)preview_popup=argv[++i];
        else if(!strcmp(argv[i],"--audio-diagnostics"))setenv("N1_RADIO_DEBUG","1",1);
        else if(!strcmp(argv[i],"--exit-after")&&i+1<argc)exit_after=atoi(argv[++i]);
        else if(!strcmp(argv[i],"--help")){puts("N1 Radio --headless --demo --start --audio-diagnostics --exit-after SECONDS\nLVGL RTL-SDR receiver: FM / AM / USB / LSB / NFM");return 0;}
        else {fprintf(stderr,"Unknown argument: %s\n",argv[i]);return 2;}
    }
    if(headless && demo){fputs("--headless cannot be combined with --demo\n",stderr);return 2;}
    const char *base=getenv("XDG_CONFIG_HOME"),*home=getenv("HOME");char directory[4000];
    if(base && *base)snprintf(directory,sizeof(directory),"%s/n1-radio",base);
    else snprintf(directory,sizeof(directory),"%s/.config/n1-radio",home?home:"/tmp");
    if(!demo && mkdir(directory,0700)!=0 && errno!=EEXIST){fprintf(stderr,"Cannot create settings directory: %s\n",strerror(errno));return 1;}
    snprintf(config_path,sizeof(config_path),"%s/settings.ini",directory);config_load(&cfg,config_path);band=infer_band();
    if(demo){config_defaults(&cfg);cfg.count=3;cfg.stations[0]=(station){100000000,MODE_WFM,"电台 01"};cfg.stations[1]=(station){7100000,MODE_AM,"短波收藏"};cfg.stations[2]=(station){118000000,MODE_AM,"VHF 收藏"};}
    signal(SIGINT,signal_exit);signal(SIGTERM,signal_exit);signal(SIGPIPE,SIG_IGN);
    if(!demo){if(!control_open(&control)){fprintf(stderr,"Cannot open control socket (another N1 Radio instance may be running): %s\n",strerror(errno));return 1;}control_ready=true;}
    if(!demo){if(!engine_init(&engine,&cfg)){fputs("Cannot start receiver thread\n",stderr);return 1;}initialized=true;}
    atexit(cleanup);
    if(headless){
        if(start_receiving){char error[256];if(!radio_validate(&cfg,error,sizeof(error))){fprintf(stderr,"%s\n",error);return 2;}engine_apply(&engine,&cfg,true);}
        uint64_t started=rds_now_ms();
        while(!exiting){poll_control();if(save_pending && rds_now_ms()>=save_due){save_pending=false;save();}if(exit_after>0 && rds_now_ms()-started>(uint64_t)exit_after*1000)break;usleep(10000);}
        cleanup();return 0;
    }
    lv_init();
    lv_display_t *display=lv_sdl_window_create(WINDOW_WIDTH,WINDOW_HEIGHT);if(!display)return 1;
    lv_sdl_window_set_zoom(display,0.5f);
    SDL_SetWindowPosition(lv_sdl_window_get_window(display),SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED);
    lv_sdl_window_set_title(display,"N1 Radio / LVGL");lv_sdl_window_set_resizeable(display,false);
    lv_group_t *group=lv_group_create();lv_group_set_default(group);
    lv_indev_t *mouse=lv_sdl_mouse_create();lv_indev_set_display(mouse,display);
    lv_indev_t *keyboard=lv_sdl_keyboard_create();lv_indev_set_group(keyboard,group);
    create_ui();if(demo && preview_popup){if(!strcmp(preview_popup,"settings"))show_hardware(NULL);else if(!strcmp(preview_popup,"preset"))show_preset(NULL);else if(!strcmp(preview_popup,"frequency"))show_frequency(NULL);}
    if(start_receiving){char error[256];if(radio_validate(&cfg,error,sizeof(error))){active=true;if(!demo)engine_apply(&engine,&cfg,true);}else tell(error);}
    lv_refr_now(display);
    SDL_ShowWindow(lv_sdl_window_get_window(display));
    uint32_t start=lv_tick_get();
    while(!exiting && lv_display_get_default()) {poll_control();lv_timer_handler();if(exit_after>0 && lv_tick_get()-start>(uint32_t)exit_after*1000)break;usleep(10000);}
    cleanup();lv_deinit();return 0;
}
