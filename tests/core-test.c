#define _POSIX_C_SOURCE 200809L
#include "radio.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
int main(void) {
 radio_config c,d;config_defaults(&c);char err[256];assert(radio_validate(&c,err,sizeof(err)));
 c.hz=7100000;c.mode=MODE_AM;assert(!radio_validate(&c,err,sizeof(err)));assert(strstr(err,"24 MHz"));
 c.input=INPUT_DIRECT_Q;assert(radio_validate(&c,err,sizeof(err)));c.hz=100000000;assert(!radio_validate(&c,err,sizeof(err)));
 c.input=INPUT_UPCONVERTER;c.hz=7100000;c.offset_hz=125000000;assert(radio_validate(&c,err,sizeof(err)));
 c.offset_hz=-1;assert(!radio_validate(&c,err,sizeof(err)));c.offset_hz=125000000;
 c.mode=MODE_USB;c.ppm=-20;c.volume=71;c.count=2;c.stations[0]=(station){7100000,MODE_USB,"短波电台"};c.stations[1]=(station){100000000,MODE_WFM,"FM, station"};
 char path[]="/tmp/n1-radio-test-XXXXXX";int fd=mkstemp(path);assert(fd>=0);close(fd);
 assert(config_save(&c,path));config_load(&d,path);assert(c.hz==d.hz && c.mode==d.mode && c.input==d.input && c.volume==d.volume && c.ppm==d.ppm);assert(d.count==2 && d.rf_gain==254);assert(!strcmp(d.stations[0].name,"短波电台"));assert(!strcmp(d.stations[1].name,"FM, station"));
 FILE *f=fopen(path,"w");assert(f);fputs("frequency=nan\nvolume=999\nppm=-9999\ninput=44\nmode=99\nstation=abc,3,bad\nstation=7100000,2,valid\n",f);fclose(f);config_load(&d,path);assert(d.hz==100000000 && d.volume==35 && d.ppm==0 && d.count==1 && d.input==INPUT_TUNER);unlink(path);
 assert(radio_sample_rate(MODE_WFM)==48000 && radio_sample_rate(MODE_AM)==24000);assert(!strcmp(radio_demod(MODE_USB),"usb"));
 puts("PASS: tuning limits, HF hardware validation, atomic config round-trip and malformed input");return 0;
}
