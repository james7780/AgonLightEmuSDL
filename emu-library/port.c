#include "port.h"
#include "cpu.h"
#include "schedule.h"
#include "../agon_vdp.h"                   // JH - agon VDP handling
#include "debug/debug.h"

// #define PORT_READ_DELAY  2
// #define PORT_WRITE_DELAY 4
#define PORT_READ_DELAY  0  // COCOACRUMBS
#define PORT_WRITE_DELAY 0  // COCOACRUMBS

/* Global APB state */
//eZ80portrange_t port_map[0x10];
eZ80portrange_t port_map[0xFF];					// JH


//#define port_range(a) (((a)>>12)&0xF) /* converts an address to a port range 0x0-0xF */

static const uint32_t port_mirrors[0x10] = {0xFF,0xFF,0xFF,0x1FF,0xFFF,0xFF,0x1F,0xFF,0x7F,0xFFF,0x7F,0xFFF,0xFF,0x7F,0x7F,0xFFF};

static uint8_t port_read(uint16_t address, uint8_t loc, bool peek) {
    // JH - TODO - Add these to the port map!
    //uint8_t vdp_status = 0;
    //uint8_t vdp_data = 0;
    if (address == 0xC5)
        {
        uint8_t vdp_status = vdp_read_status_byte();
        printf("Read %d from UART0 status port %X\n", vdp_status, address);
        //getchar();
        return vdp_status;
        }
    else if (address == 0xC0)
        {
        uint8_t vdp_data = vdp_read_serial();
        printf("Read %d from UART0 data port %X\n", vdp_data, address);
        //getchar();
        return vdp_data;
        }
    else if (address == 0x86)
        {
        printf("Read from TMR2_CTL\n");
        //getchar();
        }

    //return port_map[loc].read(address & port_mirrors[loc], peek);
		if (port_map[address].read != NULL)
	    return port_map[address].read(address, peek);
		else
			printf("UNHANDLED read from port %X\n", address);

	return 0;
}
uint8_t port_peek_byte(uint16_t address) {
    //return port_read(address, port_range(address), true);
    return port_read(address, 0, true);
}
uint8_t port_read_byte(uint16_t address) {
    uint8_t port_loc = 0; // port_range(address);
    uint8_t value;
    static const uint8_t port_read_cycles[0x10] = {2,2,2,4,3,3,3,3,3,3,3,3,3,3,3,3};

#ifdef DEBUG_SUPPORT
    if (debug.port[address] & DBG_MASK_PORT_READ) {
        debug_open(DBG_PORT_READ, address);
    }
#endif

    cpu.cycles += PORT_READ_DELAY;
    sched_process_pending_events(); /* make io ports consistent with mid-instruction state */
    value = port_read(address, port_loc, false);
    cpu.cycles += port_read_cycles[port_loc] - PORT_READ_DELAY;
    return value;
}

static void port_write(uint16_t address, uint8_t loc, uint8_t value, bool peek) {
    // JH
    if (address == 0xC0)
        {
        printf("Write %d to UART0 data port %X\n", value, address);
        //getchar();
        vdp_write_serial(value);
				return;
        }
    else if (address == 0x86)
        {
        printf("Write %d to TMR2_CTL\n", value);
        //getchar();
        }

    //port_map[loc].write(address & port_mirrors[loc], value, peek);
		if (port_map[address].write != NULL)
	    port_map[address].write(address, value, peek);
		else
			printf("UNHANDLED Write %d to port %X\n", value, address);

}

void port_poke_byte(uint16_t address, uint8_t value) {
    //port_write(address, port_range(address), value, true);
    port_write(address, 0, value, true);

}
void port_write_byte(uint16_t address, uint8_t value) {
    uint8_t port_loc = 0; //port_range(address);
    static const uint8_t port_write_cycles[0x10] = {2,2,2,4,2,3,3,3,3,3,3,3,3,3,3,3};

#ifdef DEBUG_SUPPORT
    if (debug.port[address] & (DBG_MASK_PORT_FREEZE | DBG_MASK_PORT_WRITE)) {
        if (debug.port[address] & DBG_MASK_PORT_WRITE) {
            debug_open(DBG_PORT_WRITE, address);
        }
        if (debug.port[address] & DBG_MASK_PORT_FREEZE) {
            return;
        }
    }

#endif

    cpu.cycles += PORT_WRITE_DELAY;
    sched_process_pending_events(); /* make io ports consistent with mid-instruction state */
    port_write(address, port_loc, value, false);
    cpu.cycles -= PORT_WRITE_DELAY - port_write_cycles[port_loc];
}
