#include "utils.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "kk_ihex_read.h"
#include "kk_ihex.h"


#define MEMORY_SIZE                         (16 * 1024 * 1024)
#define AUTODETECT_ADDRESS                  (~0UL)


static unsigned long    line_number         = 1L;
static unsigned long    address_offset      = 0UL;
static bool             debug_enabled       = false;
static uint8_t          *memory             = NULL;

// JH - For Windows 32/64
int getline(char *buffer, size_t bufferSize, FILE *fp)
{
	char *result = NULL;
	buffer[0] = 0;
	result = fgets(buffer, (int)bufferSize, fp);
	return (result == NULL) ? -1 : (int)strlen(buffer);
}


uint8_t *loadHex(char *filePath)
{
    FILE *infile = NULL;

    if (memory != NULL) 
    {
        free(memory);
    } /* end if */
    
    memory = calloc(MEMORY_SIZE, sizeof(uint8_t));
    if (memory == NULL)
    {
        return NULL;
    }
    else
    {
        infile = fopen(filePath, "rb");
        if (infile == NULL)
        {
            return NULL;
        }
        else
        {
            char								line[1024];
            size_t              len     = 1024;
            int                 read    = -1;
            struct  ihex_state  ihex;

            ihex_begin_read(&ihex);        

            while ((read = getline(line, len, infile)) != -1)
            {
                if (debug_enabled) 
                {
                    printf("Retrieved line of length %d :\n", read);
                    printf("%s", line);
                } /* end if */

                ihex_read_bytes(&ihex, line, read);
            } /* end while */

            //free(line);

            ihex_end_read(&ihex);
            fclose(infile);
        } /* end if */
    } /* end if */
    return memory;
} /* end loadHex */


ihex_bool_t ihex_data_read(struct ihex_state   *ihex,
                           ihex_record_type_t   type,
                           ihex_bool_t          error) 
{
    if (error) 
    {
        (void) fprintf(stderr, "Checksum error on line %lu\n", line_number);
        exit(EXIT_FAILURE);
    }
    if ((error = (ihex->length < ihex->line_length))) 
    {
        (void) fprintf(stderr, "Line length error on line %lu\n", line_number);
        exit(EXIT_FAILURE);
    }
    if (type == IHEX_DATA_RECORD) 
    {
        unsigned long address = (unsigned long) IHEX_LINEAR_ADDRESS(ihex);
        if (address < address_offset) {
            if (address_offset == AUTODETECT_ADDRESS) 
            {
                // autodetect initial address
                address_offset = address;
                if (debug_enabled) 
                {
                    (void)fprintf(stderr, "Address offset: 0x%lx\n", address_offset);
                } /* end if */
            } 
            else 
            {
                (void) fprintf(stderr, "Address underflow on line %lu\n", line_number);
                exit(EXIT_FAILURE);
            } /* end if */
        } /* end if */
        address -= address_offset;
        if (debug_enabled) 
        {
            (void) fprintf(stderr, "\naddress: %lx\n", address);
        } /* end if */
        for (size_t idx = 0; idx < ihex->length; idx++)
        {
            memory[address + idx] = ihex->data[idx];
        } /* end for */
    } 
    else if (type == IHEX_END_OF_FILE_RECORD) 
    {
        if (debug_enabled) 
        {
            // (void) fprintf(stderr, "%lu bytes written\n", file_position);
        } /* end if */
    } /* end if */
    return true;
} /* ihex_data_read */
