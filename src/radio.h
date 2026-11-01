#ifndef N1_RADIO_H
#define N1_RADIO_H
#include <stdbool.h>
#include <stdint.h>
#include <pthread.h>
#define MAX_STATIONS 16
#define STATION_NAME 80
#define RADIO_MIN_HZ 500000LL
#define RADIO_MAX_HZ 1766000000LL

typedef enum { MODE_WFM, MODE_AM, MODE_USB, MODE_LSB, MODE_NFM, MODE_COUNT } radio_mode;
typedef enum { INPUT_TUNER, INPUT_DIRECT_Q, INPUT_UPCONVERTER } radio_input;
typedef struct { int64_t hz; radio_mode mode; char name[STATION_NAME]; } station;
typedef struct {
    int64_t hz, offset_hz;
    int volume, ppm;
    bool mute;
    radio_mode mode;
    radio_input input;
    station stations[MAX_STATIONS];
    int count;
    int rf_gain; // tenths of dB; -1 = automatic
} radio_config;
typedef struct {
    pthread_mutex_t lock;
    pthread_t thread;
    radio_config config;
    bool active, quit, receiving;
    unsigned generation;
    float audio_level;
    char message[256];
    uint64_t audio_samples;
} radio_engine;

void config_defaults(radio_config *c);
bool radio_validate(const radio_config *c, char *err, unsigned size);
const char *radio_mode_name(radio_mode mode);
const char *radio_demod(radio_mode mode);
int radio_sample_rate(radio_mode mode);
void config_load(radio_config *c, const char *path);
bool config_save(const radio_config *c, const char *path);
bool engine_init(radio_engine *e, const radio_config *c);
void engine_apply(radio_engine *e, const radio_config *c, bool active);
void engine_snapshot(radio_engine *e, bool *active, bool *receiving, char *message, unsigned size, float *level);
void engine_destroy(radio_engine *e);
#endif
