#include "emu.h"
#include "mem.h"
#include "asic.h"
#include "cpu.h"
#include "cert.h"
#include "os/os.h"
#include "defines.h"
#include "schedule.h"
#include "debug/debug.h"

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#define IMAGE_VERSION 0xCECE0015

void EMSCRIPTEN_KEEPALIVE emu_exit(void) {
    cpu.abort = CPU_ABORT_EXIT;
}

void EMSCRIPTEN_KEEPALIVE emu_reset(void) {
    asic_reset();
}

bool emu_save(emu_data_t type, const char *path) {
    FILE *file = NULL;
    bool success = false;

    if (mem.flash.block == NULL || mem.ram.block == NULL || path == NULL) {
        return false;
    }

    if ((file = fopen_utf8(path, "wb"))) {
        uint32_t version = IMAGE_VERSION;
        switch (type) {
            case EMU_DATA_IMAGE:
                success = fwrite(&version, sizeof version, 1, file) == 1 && asic_save(file);
                break;
            case EMU_DATA_ROM:
                success = fwrite(mem.flash.block, 1, SIZE_FLASH, file) == SIZE_FLASH;
                break;
            case EMU_DATA_RAM:
                success = fwrite(mem.ram.block, 1, SIZE_RAM, file) == SIZE_RAM;
                break;
        }
        fclose(file);
    }

    return success;
}

emu_state_t emu_load(emu_data_t type, const char *path) {
    uint32_t version;
    emu_state_t state = EMU_STATE_INVALID;
    FILE *file = NULL;

    if (!path) {
        return state;
    }

    if (type == EMU_DATA_IMAGE) {
        file = fopen_utf8(path, "rb");

        printf("[eZ80-Emu] Loading Emulator Image...\n");

        if (!file) {
            printf("[eZ80-Emu] Image file nonexistent.\n");
            goto rerr;
        }

        if (fread(&version, sizeof(version), 1, file) != 1) goto rerr;

        if (version != IMAGE_VERSION) {
            printf("[eZ80-Emu] Error in versioning.\n");
            goto rerr;
        }

        asic_free();
        asic_init();
        asic_reset();

        if (!asic_restore(file)) {
            printf("[eZ80-Emu] Error reading image.\n");
            goto rerr;
        }

        printf("[eZ80-Emu] Loaded Emulator Image.\n");

        state = EMU_STATE_VALID;
    } else if (type == EMU_DATA_ROM) {
        bool gotType = false;
        uint16_t field_type;
        const uint8_t *outer;
        const uint8_t *current;
        const uint8_t *data;
        uint32_t outer_field_size;
        uint32_t data_field_size;
        ti_device_t type = TI84PCE;
        uint32_t offset;
        size_t size;

        printf("[eZ80-Emu] Loading ROM Image...\n");

        file = fopen_utf8(path, "rb");

        if (!file) {
            printf("[eZ80-Emu] ROM file nonexistent.\n");
            goto rerr;
        }

        if (fseek(file, 0L, SEEK_END) < 0) goto rerr;
        size = (size_t)ftell(file);
        if (size > SIZE_FLASH) {
            printf("[eZ80-Emu] Invalid ROM size (%u bytes | max %u bytes)\n", (unsigned int)size, SIZE_FLASH);
            goto rerr;
        }
        rewind(file);

        asic_free();
        asic_init();

        if (fread(mem.flash.block, size, 1, file) != 1) {
            printf("[eZ80-Emu] Error reading ROM image\n");
            goto rerr;
        }

        /* Parse certificate fields to determine model. */
        for (offset = 0x20000U; offset < 0x40000U; offset += 0x10000U) {
            outer = mem.flash.block;

            /* Outer 0x800(0) field. */
            if (cert_field_get(outer + offset, SIZE_FLASH - offset, &field_type, &outer, &outer_field_size)) break;
            if (field_type != 0x800F) continue;

            /* Inner 0x801(0) field: calculator model */
            if (cert_field_get(outer, outer_field_size, &field_type, &data, &data_field_size)) break;
            if (field_type != 0x8012 || data[0] != 0x13) break;

            /* Inner 0x802(0) field: skip. */
            data_field_size = outer_field_size - (data + data_field_size - outer);
            data = outer;
            if (cert_field_next(&data, &data_field_size)) break;
            current = data;
            if (cert_field_get(current, data_field_size, &field_type, &data, &data_field_size)) break;
            if (field_type != 0x8021) break;

            /* Inner 0x803(0) field: skip. */
            data_field_size = outer_field_size - (data + data_field_size - outer);
            data = current;
            if (cert_field_next(&data, &data_field_size)) break;
            current = data;
            if (cert_field_get(current, data_field_size, &field_type, &data, &data_field_size)) break;
            if (field_type != 0x8032) break;

            /* Inner 0x80A(0) field: skip. */
            data_field_size = outer_field_size - (data + data_field_size - outer);
            data = current;
            if (cert_field_next(&data, &data_field_size)) break;
            current = data;
            if (cert_field_get(current, data_field_size, &field_type, &data, &data_field_size)) break;
            if (field_type != 0x80A1) break;

            /* Inner 0x80C(0) field: keep. */
            data_field_size = outer_field_size - (data + data_field_size - outer);
            data = current;
            if (cert_field_next(&data, &data_field_size)) break;
            current = data;
            if (cert_field_get(current, data_field_size, &field_type, &data, &data_field_size)) break;
            if (field_type != 0x80C2) break;

            if (data[1] != 0 && data[1] != 1) break;
            type = (ti_device_t)(data[1]);

            /* If we come here, we've found something. */
            gotType = true;

            printf("[eZ80-Emu] Loaded ROM Image.\n");
            break;
        }

        state = EMU_STATE_VALID;

        if (gotType) {
            set_device_type(type);
        } else {
            set_device_type(TI84PCE);
            printf("[eZ80-Emu] Could not determine device type.\n");
            state = EMU_STATE_NOT_A_CE;
        }

        asic_reset();
    } else if (type == EMU_DATA_RAM) {
        size_t size;

        if (mem.ram.block == NULL) {
            printf("[eZ80-Emu] Emulator inactive, cannot load RAM file.\n");
            goto rerr;
        }

        /* even if we error past this, it's still okay because the rom is valid */
        state = EMU_STATE_VALID;

        file = fopen_utf8(path, "rb");
        if (!file) {
            printf("[eZ80-Emu] RAM file nonexistent.\n");
            goto rerr;
        }
        if (fseek(file, 0L, SEEK_END) < 0) goto rerr;
        size = (size_t)ftell(file);
        if (size > SIZE_RAM) {
            printf("[eZ80-Emu] Invalid RAM size (%u bytes | max %u bytes)\n", (unsigned int)size, SIZE_RAM);
            goto rerr;
        }
        rewind(file);

        if (fread(mem.ram.block, 1, size, file) != size) {
            printf("[eZ80-Emu] Error reading RAM image.\n");
            goto rerr;
        }

        printf("[eZ80-Emu] Loaded RAM Image.\n");
    }
rerr:

    if (file) {
        fclose(file);
    }

    if (state == EMU_STATE_INVALID) {
        asic_free();
    }

    return state;
}

void emu_run(uint64_t ticks) {
    sched.run_event_triggered = false;
    sched_repeat(SCHED_RUN, ticks);
    while (cpu.abort != CPU_ABORT_EXIT) {
        sched_process_pending_events();
        if (cpu.abort == CPU_ABORT_RESET) {
            cpu_transition_abort(CPU_ABORT_RESET, CPU_ABORT_NONE);
            printf("[eZ80-Emu] Reset triggered.\n");
            asic_reset();
#ifdef DEBUG_SUPPORT
            gui_debug_open(DBG_READY, 0);
#endif
        }
        if (sched.run_event_triggered) {
            break;
        }
        cpu_execute();
    }
}

void emu_set_run_rate(uint32_t rate) {
    sched_set_clock(CLOCK_RUN, rate);
}

uint32_t emu_get_run_rate() {
    return sched_get_clock_rate(CLOCK_RUN);
}
