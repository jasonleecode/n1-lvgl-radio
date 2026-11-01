#ifndef N1_RADIO_RDS_H
#define N1_RADIO_RDS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

typedef enum {RDS_IDLE, RDS_NOT_FM, RDS_WAITING, RDS_NO_SIGNAL, RDS_LOCKED,
              RDS_STALE, RDS_UNAVAILABLE, RDS_ERROR} rds_status;
typedef struct {
    rds_status state;
    char ps[128],text[512],pi[16],program_type[128];
    uint64_t groups,last_group_ms,started_ms;
} rds_info;
#define RDS_QUEUE_SIZE 131072
#define RDS_LINE_SIZE 8192
typedef struct {
    pid_t pid;
    int input,output;
    unsigned char pending[RDS_QUEUE_SIZE];
    size_t begin,length;
    char line[RDS_LINE_SIZE];
    size_t line_length;
    bool discard_line;
    rds_info info;
} rds_decoder;

uint64_t rds_now_ms(void);
bool rds_parse_json(rds_info *info,const char *line,size_t length,uint64_t now);
void rds_expire(rds_info *info,uint64_t now);
bool rds_decoder_start(rds_decoder *d,unsigned rate);
void rds_decoder_poll(rds_decoder *d);
void rds_decoder_push(rds_decoder *d,const void *data,size_t bytes);
void rds_decoder_stop(rds_decoder *d);
#endif
