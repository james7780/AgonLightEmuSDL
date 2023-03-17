#ifndef AGON_VDP_H
#define AGON_VDP_H

#ifdef __cplusplus
extern "C" {
#endif

#include "emu-library/defines.h"
#include <stdint.h>
#include <stdbool.h>

/// Initilaise VDP ("boot" VDP)
extern int vdp_init();

/// Read from VDP status (port 0xC5 (197))
extern uint8_t vdp_read_status_byte();

/// Read from VDP serial port (UART) (port 0xC0 (192))
extern uint8_t vdp_read_serial();

/// Write to VDP serial port (UART)
extern void vdp_write_serial(uint8_t c);

/// Allow VDP to run internal processing (get keys etc)
extern void vdp_tick();

/// Tidy up VDP resources
extern void vdp_shutdown();

#ifdef __cplusplus
}
#endif

#endif
