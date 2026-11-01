#ifndef RADIO_CONTROL_H
#define RADIO_CONTROL_H
#include "radio.h"
#include <json-c/json.h>
typedef struct {int listener,lock,clients[8];uint64_t deadlines[8];char path[108];} radio_control;
bool control_open(radio_control *s);
void control_close(radio_control *s);
void control_poll(radio_control *s,radio_config *c,radio_engine *e,bool *updated,bool *quit);
#endif
