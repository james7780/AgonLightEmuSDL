#include "timers.h"
#include "control.h"
#include "cpu.h"
#include "emu.h"
#include "schedule.h"
#include "interrupt.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

/* JH - Documentation for TI84 timers:
https://wikiti.brandonw.net/index.php?title=84PCE:Ports:7000

3 32-bit timers, mem-mapped to $7000 - $7038

$7000       Timer 1 counter (R/W)
$7004       Timer 1 reset (reload) value
$7008       Timer 1 Match value 1
$700C       Timer 1 Match value 2
$7010 - $701F   Timer 2
$7020 - $702F   Timer 3
$7030       Control register (12-bit, 4 bits for each timer)
            Bit 0 - timer 1 enable
            Bit 1 - timer 1 clock source (32768 Hz xtal, or CPU clock)
            Bit 2 - timer 1 interrupt enable (when counter hits 0)
            Bit 9 - timer 1 Count direction (up or down)
            Bits 3, 4, 5, 10 - timer 2  
            Bits 6, 7, 8, 11 - timer 3  
$7034       Intrrrupt status for all 3 timers (12 bits, 3 bits per timer)
$7038       Interrupt mask (as per interrupt status)
$7038 (3C?) (read)    Revision number
*/



/* Global GPT state */
general_timers_state_t gpt;

static void ost_event(enum sched_item_id id) {
    static const int ost_ticks[4] = { 73, 153, 217, 313 };
    intrpt_set(INT_OSTIMER, gpt.osTimerState);
    sched_repeat(id, gpt.osTimerState ? 1 : ost_ticks[control.ports[0] & 3]);
    gpt.osTimerState = !gpt.osTimerState;
}

static uint32_t gpt_peek_counter(int index) {
    enum sched_item_id id = SCHED_TIMER1 + index;
    timer_state_t *timer = &gpt.timer[index];
    uint32_t invert = (gpt.control >> (9 + index) & 1) ? ~0 : 0;
    if (!(gpt.control >> (index * 3) & 1) || !sched_active(id)) {
        return timer->counter;
    }
    if (gpt.reset & 1 << index) {
        gpt.reset &= ~(1 << index);
        return invert;
    }
    return timer->counter + ((sched_ticks_remaining(id) + invert) ^ invert);
}

static void gpt_restore_state(enum sched_item_id id) {
    int index = id - SCHED_TIMER1;
    gpt.timer[index].counter = gpt_peek_counter(index);
}

static uint64_t gpt_next_event(enum sched_item_id id) {
    int index = id - SCHED_TIMER1;
    struct sched_item *item = &sched.items[id];
    timer_state_t *timer = &gpt.timer[index];
    uint64_t delay;
    gpt.reset &= ~(1 << index);
    if (gpt.control >> (3*index) & 1) {             // Timer n enabled in control bits
        int event;
        uint32_t counter = timer->counter, invert, status = 0, next;
        invert = (gpt.control >> (9 + index) & 1) ? ~0 : 0;     // -1 if coutning up , 0 if counting down?
        for (event = 0; event <= 1; event++) {
            if (counter == timer->match[event]) {
                status |= 1 << event;                           // Counter hit a match value? Set int status reg
            }
        }
        if (counter == invert) {                                // Counter hit 0 (coutning down), or 0xFFFFFFF (coutnign up)
            gpt.reset |= 1 << index;
            if (gpt.control >> (3*index) & 1 << 2) {
                status |= 1 << 2;                               // Counter expired? Set status "Overflow" bit
            }
            counter = timer->reset;                             // Set counter to relaod value
        } else {
            counter -= invert | 1;                              // Decrement/increment counter
        }
        if (status) {                                                   // Counter hti a match or overflowed/underflowed
            if (!sched_active(SCHED_TIMER_DELAY)) {
                sched_repeat_relative(SCHED_TIMER_DELAY, id, 0, 2);     // Add something to the scheduler
            }
            delay = sched_cycle(SCHED_TIMER_DELAY) - sched_cycle(id);
            assert(delay <= 2);
            gpt.delayStatus |= status << (((2 - delay)*3 + index)*3);       // Set up interrupt after a small delay?
            gpt.delayIntrpt |= 1 << (3*(4 - delay) + index);
        }
        item->clock = (gpt.control >> index*3 & 2) ? CLOCK_32K : CLOCK_CPU;
        if (gpt.reset & 1 << index) {
            next = 0;                           // counter had exired above
            timer->counter = counter;           // Reload counter
        } else {
            next = counter ^ invert;
            timer->counter = invert;
        }
        for (event = 1; event >= 0; event--) {                          // Handle timer "match"
            uint32_t temp = (counter ^ invert) - (timer->match[event] ^ invert);
            if (temp < next) {
                next = temp;
                timer->counter = timer->match[event];
            }
        }
        return (uint64_t)next + 1;
    }
    return 0;
}

static void gpt_refresh(enum sched_item_id id) {
    sched.items[id].clock = CLOCK_CPU;
    sched_set(id, 0); /* dummy activate to current cycle */
    uint64_t next_event = gpt_next_event(id);
    if (next_event) {
        sched_set(id, next_event);
    } else {
        sched_clear(id);
    }
}

static void gpt_event(enum sched_item_id id) {
    sched_repeat(id, 0); /* re-activate to event cycle */
    uint64_t next_event = gpt_next_event(id);
    if (next_event) {
        sched_repeat(id, next_event);
    } else {
        sched_clear(id);
    }
}

static void gpt_delay(enum sched_item_id id) {
    gpt.status |= gpt.delayStatus & ((1 << 9) - 1);
    for (int index = 0; index < 3; index++) {
        intrpt_set(INT_TIMER1 << index, gpt.delayIntrpt & 1 << index);
    }
    gpt.delayStatus >>= 9;
    if (gpt.delayStatus || gpt.delayIntrpt) {
        sched_repeat(id, 1);
    }
    gpt.delayIntrpt >>= 3;
}

// JH - What this seems to do is call the specified function with (SCHED_TIMER1 + which), but if
//      which = 3, then it call the specified function 3 times, once for each timer
static void gpt_some(int which, void update(enum sched_item_id id)) {
    int index = which < 3 ? which : 0;
    do {
        update(SCHED_TIMER1 + index);
    } while (++index < which);
}

static uint8_t gpt_read(uint16_t address, bool peek) {
    uint8_t value = 0;
    (void)peek;

    // All timer registers can be read

    if (address < 0x30 && (address & 0xC) == 0)
        {
        // Get the relevant byte of the relevant 32-bit counter (tied to the scheduler)
        value = read8(gpt_peek_counter(address >> 4 & 3), (address & 3) << 3);
        }
    else if (address < 0x40)
        {
        // Just read the relevant byte from the relevant timer register
        value = ((uint8_t *)&gpt)[address];
        }

    return value;
}

static void gpt_write(uint16_t address, uint8_t value, bool poke) {
    (void)poke;

    if (address >= 0x34 && address < 0x38)
        {
        // Reset interrupt status bits for the 3 timers
        ((uint8_t *)&gpt)[address] &= ~value;
        }
    else if (address < 0x3C)
        {
        // Writing to a timer counter? (Interesting adding a bool to an int32!)
        bool counter_delay = address < 0x30 && (address & 0xC) == 0;
        cpu.cycles += counter_delay;
        int timer = address >> 4 & 3;               // which timer (0 to 3, 3 meaning "all")
        gpt_some(timer, gpt_restore_state);     // calls gpt_restore_state(SCHED_TIMER1 + timer)
        // NB: Whoever wrtoe this code is a freaking tosser
        // If we are writing to 0x30, then gpt_some() above updates all 3 timers
        ((uint8_t *)&gpt)[address] = value;      // update local timer state
        gpt_some(timer, gpt_refresh);           // calls gpt_refresh(SCHED_TIMER1 + timer)      
        cpu.cycles -= counter_delay;
        }

}

void gpt_reset() {
    enum sched_item_id id;
    memset(&gpt, 0, sizeof(gpt));
    gpt.revision = 0x00010801;          // revision 1.8.1
    // Set up the timers in the scheduling system
    sched.items[SCHED_TIMER_DELAY].callback.event = gpt_delay;
    sched.items[SCHED_TIMER_DELAY].clock = CLOCK_CPU;
    for (id = SCHED_TIMER1; id <= SCHED_TIMER3; id++)
        {
        sched.items[id].callback.event = gpt_event;
        gpt_refresh(id);
        }
    sched.items[SCHED_OSTIMER].callback.event = ost_event;          // JH - Don't need this in Agon? (Or use JUST OS timer via Agon timer 2?)
    sched.items[SCHED_OSTIMER].clock = CLOCK_32K;
    sched_set(SCHED_OSTIMER, 0);
    printf("[eZ80-Emu] GPT reset.\n");
}

static const eZ80portrange_t device = {
    .read  = gpt_read,
    .write = gpt_write
};

eZ80portrange_t init_gpt(void) {
    printf("[eZ80-Emu] Initialized General Purpose Timers...\n");
    return device;
}

bool gpt_save(FILE *image) {
    return fwrite(&gpt, sizeof(gpt), 1, image) == 1;
}

bool gpt_restore(FILE *image) {
    return fread(&gpt, sizeof(gpt), 1, image) == 1;
}
