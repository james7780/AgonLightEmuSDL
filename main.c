// Agon Light emulator main exe  

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "getopt.h"							// JH - Local copy for Windows in this same folder

#include "asic.h"
#include "bus.h"
#include "cpu.h"
#include "mem.h"
#include "schedule.h"

#include "agon_vdp.h"

#include "zdis.h"

#include "utils.h"

// #define MAX_RESET_PROCS 20

uint8_t *memory = NULL;
struct zdis_ctx ctx;
char disassemblyLine[256] = {0};
int idx = 0;


/* ************************************************************************ */

enum Options {
    SUFFIXED_IMM = 1 << 0,
    DECIMAL_IMM  = 1 << 1,
    MNE_SPACE    = 1 << 2,
    ARG_SPACE    = 1 << 3,
    COMPUTE_REL  = 1 << 4,
    COMPUTE_ABS  = 1 << 5,
};

int myputchar (int __c)
{
    disassemblyLine[idx++] = __c;
    disassemblyLine[idx] = 0;
    // putchar(__c);

    return 0;
}

static int read(struct zdis_ctx *ctx, uint32_t addr) 
{
    return ctx->zdis_user_ptr[addr];
}

static bool put(struct zdis_ctx *ctx, enum zdis_put kind, int32_t val, bool il) {
    char pattern[8], *p = pattern;
    char resultStr[256] = {0};

    switch (kind) {
        case ZDIS_PUT_REL: // JR/DJNZ targets
            val += ctx->zdis_end_addr;
            if (ctx->zdis_user_size & COMPUTE_REL) {
                return put(ctx, ZDIS_PUT_WORD, val, il);
            }
            if (myputchar('$') == EOF) {
                return false;
            }
            val -= ctx->zdis_start_addr;
            // fallthrough
        case ZDIS_PUT_OFF: // immediate offsets from index registers
            if (val > 0) {
                *p++ = '+';
            } else if (val < 0) {
                *p++ = '-';
                val = -val;
            } else {
                return true;
            }
            // fallthrough
        case ZDIS_PUT_BYTE: // byte immediates
        case ZDIS_PUT_PORT: // immediate ports
        case ZDIS_PUT_RST: // RST targets
            if (ctx->zdis_user_size & DECIMAL_IMM) {
                *p++ = '%';
                *p++ = 'u';
                if (ctx->zdis_user_size & SUFFIXED_IMM) {
                    *p++ = 'd';
                }
            } else {
                *p++ = ctx->zdis_user_size & SUFFIXED_IMM ? '0' : '$';
                *p++ = '%';
                *p++ = '0';
                *p++ = '2';
                *p++ = ctx->zdis_lowercase ? 'x' : 'X';
                if (ctx->zdis_user_size & SUFFIXED_IMM) {
                    *p++ = 'h';
                }
            }
            *p = '\0';
            sprintf(resultStr, pattern, val);
            strcat(disassemblyLine, resultStr);
            idx = strlen(disassemblyLine);
            return true;
            // return printf(pattern, val) >= 0;
        case ZDIS_PUT_ABS: // JP/CALL immediate targets
            if (ctx->zdis_user_size & COMPUTE_ABS) {
                int32_t extend = il ? 8 : 16;
                return myputchar('$') != EOF && put(ctx, ZDIS_PUT_OFF, (int32_t)(val - ctx->zdis_start_addr) << extend >> extend, il);
            }
            // fallthrough
        case ZDIS_PUT_WORD: // word immediates (il ? 24 : 16) bits wide
        case ZDIS_PUT_ADDR: // load/store immediate addresses
            if (ctx->zdis_user_size & DECIMAL_IMM) {
                *p++ = '%';
                *p++ = 'u';
                if (ctx->zdis_user_size & SUFFIXED_IMM) {
                    *p++ = 'd';
                }
            } else {
                *p++ = ctx->zdis_user_size & SUFFIXED_IMM ? '0' : '$';
                *p++ = '%';
                *p++ = '0';
                *p++ = il ? '6' : '4';
                *p++ = ctx->zdis_lowercase ? 'x' : 'X';
                if (ctx->zdis_user_size & SUFFIXED_IMM) {
                    *p++ = 'h';
                }
            }
            *p = '\0';
            sprintf(resultStr, pattern, val);
            strcat(disassemblyLine, resultStr);
            idx = strlen(disassemblyLine);
            return true;
            // return printf(pattern, val) >= 0;
        case ZDIS_PUT_CHAR: // one character of mnemonic, register, or parentheses
            return myputchar(val) != EOF;
        case ZDIS_PUT_MNE_SEP: // between mnemonic and arguments
            return myputchar(ctx->zdis_user_size & MNE_SPACE ? ' ' : '\t') != EOF;
        case ZDIS_PUT_ARG_SEP: // between two arguments
            sprintf(resultStr, "%s", ARG_SPACE ? ", " : ",");
            strcat(disassemblyLine, resultStr);
            idx = strlen(disassemblyLine);
            return true;
            // return fputs(ctx->zdis_user_size & ARG_SPACE ? ", " : ",", stdout) != EOF;
        case ZDIS_PUT_END: // at end of instruction
            return myputchar('\n') != EOF;
    }
    // return false for error
    return false;
}

/* ************************************************************************ */


void eZ80_init(void)
{
    asic_free();
    asic_init();
    asic_reset();

    sched_set_clock(CLOCK_RUN, 1000);
    set_cpu_clock(1);

    ctx.zdis_end_addr = 0xFFFFFF;  
    ctx.zdis_implicit = false;
    ctx.zdis_adl = true;
    ctx.zdis_user_size &= ~SUFFIXED_IMM;
    ctx.zdis_user_size &= ~DECIMAL_IMM;
    ctx.zdis_user_size &= ~MNE_SPACE;
    ctx.zdis_user_size |= ARG_SPACE;
}


void printUsage(void)
{
    printf("Usage: eZ80_emu -H filename.hex\n");
}


void printBytes(struct zdis_ctx *ctx)
{
    int         padding         = 5;
    uint32_t    startAddress    = ctx->zdis_start_addr;

    while (startAddress < ctx->zdis_end_addr) 
    {
        printf("%02X ", memory[startAddress]);
        startAddress++;
        padding--;
    } /* end while */
    while (padding > 0)
    {
        printf("   ");
        padding--;
    } /* end while */
    printf("\t");
}


void printRegisters(eZ80registers_t registers)
{
    printf("    A =     \033[35m%02X\033[37m | F =     \033[35m%02X\033[37m | BC = \033[35m%06X\033[37m | DE = \033[35m%06X\033[37m | HL = \033[35m%06X\033[37m\n", registers.A, registers.F, registers.BC, registers.DE, registers.HL);
    printf("    A'=     \033[35m%02X\033[37m | F'=     \033[35m%02X\033[37m | BC'= \033[35m%06X\033[37m | DE'= \033[35m%06X\033[37m | HL'= \033[35m%06X\033[37m\n", registers.A, registers.F, registers.BC, registers.DE, registers.HL);
    printf("    IX= \033[35m%06X\033[37m | IY= \033[35m%06X\033[37m | SPL= \033[35m%06X\033[37m | SPS=   \033[35m%04X\033[37m | PC = \033[35m%06X\033[37m\n", registers.IX, registers.IY, registers.SPL, registers.SPS, registers.PC);
    printf("    I =     \033[35m%02X\033[37m | R =     \033[35m%02X\033[37m | ADL=      \033[35m%01X\033[37m | MADL=     \033[35m%01X\033[37m\n", registers.I, registers.R, cpu.ADL, cpu.MADL);
    printf("    MBASE = \033[35m%02X\033[37m | IEF1=    \033[35m%01X\033[37m | IEF2=     \033[35m%01X\033[37m\n", registers.MBASE, cpu.IEF1, cpu.IEF2);
    printf("    -----------------------------------------------------------------\n");
}


void emu_run(uint64_t ticks) {
    sched.run_event_triggered = false;
    sched_repeat(SCHED_RUN, ticks);
    while (cpu.abort != CPU_ABORT_EXIT) 
    {
        idx = 0;

				printf("\033[1m\033[31m%06x\033[33m\t", cpu.registers.PC);
        ctx.zdis_start_addr = cpu.registers.PC;
        ctx.zdis_end_addr = cpu.registers.PC;
        disassemblyLine[idx] = 0;
        zdis_put_inst(&ctx);

        printBytes(&ctx);

        printf("\033[36m%s\033[37m", disassemblyLine);

//       printRegisters(cpu.registers);

        sched_process_pending_events();
        if (cpu.abort == CPU_ABORT_RESET) 
        {
            cpu_transition_abort(CPU_ABORT_RESET, CPU_ABORT_NONE);
            printf("[eZ80-Emu] Reset triggered.\n");
            asic_reset();
        } /* end if */
        if (sched.run_event_triggered) 
        {
            break;
        } /* end if */
        cpu_execute();

        if (cpu.registers.PC > 0xB668)     // MOS (debug) length        // was 0x2A0)
            cpu.abort = CPU_ABORT_EXIT;

				// Allow vdp to process input and do it's thing
				vdp_tick();
    } /* end while */
}


int main(int argc, char **argv)
{
    int c;
    int option_index = 0;
    //uint32_t end = 0;

    //static const struct option long_options[] = 
    //{
    //    {"help", no_argument,       0,  'h' },
    //    {"Hex",  required_argument, 0,  'H' },
    //    {}
    //};

    printf("Agon Light eZ80 emulator v0.0.1\n");

		// JH - Hardcode loading of MOS image
    printf("Loading MOS hex image...\n");
		memory = loadHex("MOS_debug.hex");
		if (!memory)
			{
			exit(EXIT_FAILURE);
			}

/* other faff
		c = 0;
    while (c > -1) 
		{
    //c = getopt_long(argc, argv, "hH:", long_options, &option_index);
		c = getopt(argc, argv, "hH:");
    if (c > -1)
	    {
			switch (c) 
				{
				case 'h':
					// fprintf(stdout, "help: yes\n");
					printUsage();
					exit(0);
					break;
				case 'H':
					fprintf(stdout, "Hex: %s\n", optarg);
					memory = loadHex(optarg);
					if (memory == NULL)
						{
						exit(EXIT_FAILURE);
						}
					// mem_install_memory(memory);
					break;
				default:
					break;
				}
			}
		}
*/

    eZ80_init();
		// JH - Init the ESP32 VDP
    if (vdp_init() != 0)
			return 1;

    ctx.zdis_read = read; // callback for getting bytes to disassemble
    ctx.zdis_put = put; // callback for processing disassembly output
    ctx.zdis_start_addr = 0; // starting address of the current instruction
    ctx.zdis_end_addr = 0; // ending address of the current instruction
    ctx.zdis_lowercase = true; // automatically convert ZDIS_PUT_CHAR characters to lowercase
    ctx.zdis_implicit = true; // omit certain destination arguments as per z80 style assembly
    ctx.zdis_adl = false; // default word width when not overridden by suffix
    ctx.zdis_user_ptr = memory; // arbitrary use
    ctx.zdis_user_size = 0; // arbitrary use

    emu_run(1);

		vdp_shutdown();

    return 0;
}


// JH NOtes:
// 1. IN0 (197) - Read UART0 status (ie: VDP ready to send)  (0 if ready, or nothing left to receive)
// 2. OUT0	(192),A - write to UART0 (ie: VDP)
// 3. IN0 (192), A - read from UART0 (VDP)