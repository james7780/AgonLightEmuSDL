#ifndef TIMERS_H
#define TIMERS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "port.h"
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

typedef struct timer_state {
    uint32_t counter, reset, match[2];							// 16 bytes
} timer_state_t;

typedef struct general_timers_state {
    timer_state_t timer[3];									// 48 bytes (0x00 to 0x2F)
    uint32_t control, status, mask, revision;				// ctrl =  0x30, status = 0x34, mask = 0x38, revision = 0x3C						
    uint32_t delayStatus, delayIntrpt;
    uint8_t reset;
    bool osTimerState;
} general_timers_state_t;

extern general_timers_state_t gpt;

eZ80portrange_t init_gpt(void);
void gpt_reset(void);
bool gpt_restore(FILE *image);
bool gpt_save(FILE *image);

#ifdef __cplusplus
}
#endif

#endif
