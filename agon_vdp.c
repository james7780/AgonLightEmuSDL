// Agon Light VDP emulation
// James Higgs 2023

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "agon_vdp.h"
#include "font_agon.h"					// fotn data from VDP src
#include "debug/debug.h"

#include <SDL.h>


#define UART_LSR_DATA_READY				((unsigned char)0x01)		//!< Data ready indication in LSR.
#define UART_LSR_OVERRRUNERR			((unsigned char)0x02)		//!< Overrun error indication in LSR.
#define UART_LSR_PARITYERR				((unsigned char)0x04)		//!< Parity error indication in LSR.
#define UART_LSR_FRAMINGERR				((unsigned char)0x08)		//!< Framing error indication in LSR.
#define UART_LSR_BREAKINDICATIONERR		((unsigned char)0x10)		//!< Framing error indication in LSR.
#define UART_LSR_THREMPTY				((unsigned char)0x20)		//!< Transmit Holding Register empty indication in LSR.
#define UART_LSR_TEMT					((unsigned char)0x40)		//!< Transmit is empty.
#define UART_LSR_FIFOERR				((unsigned char)0x80)		//!< Error in FIFO indication in LSR.

#define PACKET_GP				0		// General poll data
#define PACKET_KEYCODE			1		// Keyboard data
#define PACKET_CURSOR			2		// Cursor positions
#define PACKET_SCRCHAR			3		// Character read from screen
#define PACKET_SCRPIXEL			4		// Pixel read from screen
#define PACKET_AUDIO			5		// Audio acknowledgement
#define PACKET_MODE				6		// Get screen dimensions

#define AUDIO_CHANNELS			3		// Number of audio channels
#define PLAY_SOUND_PRIORITY 	3		// Sound driver task priority with 3 (configMAX_PRIORITIES - 1) being the highest, and 0 being the lowest.


#define VDP_SERIAL_OUTPUT_BUFFER_SIZE 100

static uint8_t vdp_serial_output_buffer[VDP_SERIAL_OUTPUT_BUFFER_SIZE];        // VDP output serial buffer
static uint8_t vdp_serial_output_queue_len;																		// Current serial buffer status/length

static uint8_t vdp_serial_input_buffer[VDP_SERIAL_OUTPUT_BUFFER_SIZE];        // VDP intput serial buffer
static uint8_t vdp_serial_input_queue_len;																		// Current input serial buffer length



#define TEXT_COLUMNS    80
#define TEXT_ROWS       24

// Need to maintain VDP state for:

uint8_t screenBufferText[TEXT_ROWS][TEXT_COLUMNS]; 

struct {
		uint8_t screenMode;
		uint8_t cursorX;
		uint8_t cursorY;
		uint8_t cursorEnabled;

} VDP_State;

//VDP_State_t VDP_State;

// SDL stuff
static SDL_Window *sdlWindow = NULL;
static SDL_Surface *sdlSurface = NULL;
static SDL_Renderer *sdlRenderer = NULL;

#define CHARWIDTH		8
#define CHARHEIGHT	8

// Forward declarations
void handle_VDU_command();
void drawChar(uint8_t c);
void drawString(char *s);

/// Add a character to the output serial buffer
/// @param c                Char to add to the output buffer
static void vdp_queue_char(uint8_t c)
{
    if (vdp_serial_output_queue_len < VDP_SERIAL_OUTPUT_BUFFER_SIZE)
        {
        vdp_serial_output_buffer[vdp_serial_output_queue_len] = c;
        vdp_serial_output_queue_len++;
        }
    else
        printf("vdp_queue_char: Buffer length exceeded!\n");
}

/// Read a character to the output serial buffer
/// @return                Char at the front of the queue
static uint8_t vdp_unqueue_char()
{
    uint8_t i;
    uint8_t retval = 0;
    if (vdp_serial_output_queue_len > 0)
        {
        retval = vdp_serial_output_buffer[0];
        // shift queue
        for (i = 0; i < vdp_serial_output_queue_len; i++)
            vdp_serial_output_buffer[i] = vdp_serial_output_buffer[i + 1];

        vdp_serial_output_queue_len--;
        }
    else
        {
        printf("vdp_unqueue_char: Buffer empty!\n");
        }

    return retval;
}

/// Initilaise VDP ("boot" VDP)
int vdp_init() {
    printf("vdp_init()\n");


	// Initialize SDL. SDL_Init will return -1 if it fails.
	if ( SDL_Init( SDL_INIT_EVERYTHING ) < 0 ) {
		printf("Error initializing SDL: %s\n", SDL_GetError());
		system("pause");
		// End the program
		return 1;
	} 

	// Create our window
	sdlWindow = SDL_CreateWindow( "Agon Light Emu 0.001", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 640, 480, SDL_WINDOW_SHOWN );

	// Make sure creating the window succeeded
	if ( !sdlWindow ) {
		printf("Error creating window: %s\n", SDL_GetError());
		system("pause");
		// End the program
		return 1;
	}

	// Get the surface from the window
	sdlSurface = SDL_GetWindowSurface( sdlWindow );

	// Make sure getting the surface succeeded
	if ( !sdlSurface ) {
		printf("Error getting surface: %s\n", SDL_GetError());
		system("pause");
		// End the program
		return 1;
	}

	sdlRenderer = SDL_CreateSoftwareRenderer(sdlSurface);
	if ( !sdlRenderer ) {
		printf("Error getting SDL renderer: %s\n", SDL_GetError());
		system("pause");
		// End the program
		return 1;
	}

 // textBitmap = SDL_LoadBMP("agon_font.bmp");
	//if (!textBitmap)
	//	return 1;

	// Fill the window with a black rectangle
	SDL_FillRect( sdlSurface, NULL, SDL_MapRGB( sdlSurface->format, 0, 0, 0 ) );

	// Update the window display
	SDL_UpdateWindowSurface( sdlWindow );




	// Wait
	//system("pause");

	//// Destroy the window. This will also destroy the surface
	//SDL_DestroyWindow( window );

	//// Quit SDL
	//SDL_Quit();
	//
	//// End the program
	//return 0;

	// Setup VDP state
	VDP_State.screenMode = 0;
	VDP_State.cursorX = 0;
	VDP_State.cursorY = 0;
	VDP_State.cursorEnabled = 1;


    // Clear screen
    memset(screenBufferText, 0, TEXT_COLUMNS * TEXT_ROWS);

    // Write VDP version to the screen
   	drawString("Agon Quark emulated VDP Version 1.02");
    drawChar('\n');

    vdp_serial_output_queue_len = 0;
    vdp_serial_input_queue_len = 0;

    //memset(vdp_output_buffer, blah blah blah);

    // 1. Put Esc char (27) in output serial buffer to signal eZ80 that we are ready
    vdp_queue_char(27);

	return 0;
}

/// Tidy up VDP resources
void vdp_shutdown()
{
	if (sdlWindow)
		{
		// Destroy the window. This will also destroy the surface
		SDL_DestroyWindow( sdlWindow );

		// Quit SDL
		SDL_Quit();
		}
}

/// Read from VDP status (port 0xC5 (197))
/// @return         VDP status
uint8_t vdp_read_status_byte() {
    // TODO - Check this and correct!
    // Should return 1 if data available, 0 if no data pending
    uint8_t status = UART_LSR_TEMT;                     // Set incoming transmit empty, so CPU will send data
    if (vdp_serial_output_queue_len > 0)
        status |= UART_LSR_DATA_READY;
    
    return status;               // Nothing in queue
}

/// Read from VDP serial port (UART) - Read from CPU!
/// @return         Byte at head of VDP output serial queue
uint8_t vdp_read_serial() {
    if (vdp_serial_output_queue_len > 0)
        {
        return vdp_unqueue_char();
        }

    return 0;
}

/// Write to VDP serial port (UART) - ie Write from CPU!
/// @param[in] c         Byte to send to VDP
void vdp_write_serial(uint8_t c)
{
    printf("vdp_write_serial: %d\n", c);
		vdp_serial_input_buffer[vdp_serial_input_queue_len] = c;
		vdp_serial_input_queue_len++;

		// NOw handled in vdu_tick()
    //handle_VDU_command(c);
}

/// Remove n bytes from the serial input queue (ie: when they have been processed)
/// @param[in] count				Number of bytes to unqueue
void vdp_unqueue_input(uint8_t count)
{
	// shift input queue
	if (count > vdp_serial_input_queue_len)
		{
		printf("Error: vdp_unqueue_input - underflow!\n");
		system("pause");
		return;
		}

	int i = 0;
	for (i = 0; i < (vdp_serial_input_queue_len - count); i++)
			vdp_serial_input_buffer[i] = vdp_serial_input_buffer[i + count];

	vdp_serial_input_buffer[i] = 0;			// put null marker in there fro debugging
	vdp_serial_input_queue_len -= count;
}

// Redraw the text screen buffer to the emulators console
void drawTextScreen()
{
    system("clear");            // Linux only?

    for (int y = 0; y < TEXT_ROWS; y++)
        {
        for (int x = 0; x < TEXT_COLUMNS; x++)
            {
            putchar(screenBufferText[y][x]);
            }
        printf("\n");
        }
}

void scroll()
{
		// TODO SDL


    memcpy(screenBufferText, screenBufferText + TEXT_COLUMNS, (TEXT_COLUMNS * (TEXT_ROWS - 1)));
    //memset(screenBufferText + (TEXT_COLUMNS * (TEXT_ROWS - 1), 0, TEXT_COLUMNS);
    memset(screenBufferText[TEXT_ROWS - 1], 0, TEXT_COLUMNS);

    drawTextScreen();
}

void cursorUp()
{
    if (VDP_State.cursorY > 0)
        VDP_State.cursorY--;
}

void cursorDown()
{
    if (VDP_State.cursorY == TEXT_ROWS - 1)
        scroll();
    else
        VDP_State.cursorY++;
}

void cursorRight()
{
    VDP_State.cursorX++;
  	if(VDP_State.cursorX >= TEXT_COLUMNS)
        {
    	cursorDown();           // scroll if neccessary
    	VDP_State.cursorX = 0;
        }
}

void cursorLeft()
{
    if (VDP_State.cursorX > 0)
        VDP_State.cursorX--;
}

void cursorHome()
{
	VDP_State.cursorX = 0;
}

void cursorTab(uint8_t x, uint8_t y)
{
	VDP_State.cursorX = x;
	VDP_State.cursorY = y;
}

void cls()
{
	// Fill the window with a black rectangle
	SDL_FillRect( sdlSurface, NULL, SDL_MapRGB( sdlSurface->format, 0, 0, 0 ) );

	// Update the window display
	SDL_UpdateWindowSurface( sdlWindow );
}

void vdu_mode(uint8_t mode)
{
	VDP_State.screenMode = mode;
}

// Forward declarations
void vdu_sys_video();
void vdu_sys_sprites();

/// Handle VDU 23 commands
void vdu_sys()
{
	uint8_t mode = vdp_serial_input_buffer[1];
//	debug_log("vdu_sys: %d\n\r", mode);
	//
	// If mode < 32, then it's a system command
	//
	if(mode < 32)
		{
		switch(mode)
			{
			case 0x00:						// VDU 23, 0
				if (vdp_serial_input_queue_len >= 3)
					{
					vdu_sys_video();			// Video system control
					// Does it's own unqueing
					}
				break;
			case 0x01:						// VDU 23, 1
				if (vdp_serial_input_queue_len >= 3)
					{
					VDP_State.cursorEnabled = vdp_serial_input_buffer[2];	// Cursor control
					vdp_unqueue_input(3);
					}
				break;
			case 0x07:						// VDU 23, 7
				if (vdp_serial_input_queue_len >= 5)
					{
					//vdu_sys_scroll();			// Scroll (23, 7, ext, dirn, movement)
					vdp_unqueue_input(3);
					}
				break;
			case 0x1B:						// VDU 23, 27
				system("pause");
				// TODO
				//vdu_sys_sprites();			// Sprite system control
				break;
			}
		}
	//
	// Otherwise, do
	// VDU 23,mode,n1,n2,n3,n4,n5,n6,n7,n8
	// Redefine character with ASCII code mode
	//
	else
		{
		// Warning - can crash the emulator!
		if (vdp_serial_input_queue_len >= 10)
			{
			uint8_t *ptr = FONT_AGON_DATA[mode * 8];
			for(int i = 0; i < 8; i++)
				{
				*ptr++ = vdp_serial_input_buffer[i + 2];
				}
			vdp_unqueue_input(10);
			}
		}
}

// VDU 23, 0: VDP control
// These can send responses back; the response contains a packet # that matches the VDU command mode byte
//
void vdu_sys_video() {
		uint8_t mode = vdp_serial_input_buffer[2];
  	switch(mode) {
		case PACKET_KEYCODE: 		// VDU 23, 0, 1, layout
			if (vdp_serial_input_queue_len >= 4)
				{
				uint8_t layout =  vdp_serial_input_buffer[2];
				switch(layout) {
					case 1:				// US Layout
						//PS2Controller.keyboard()->setLayout(&fabgl::USLayout);
						break;
					case 2:				// German Layout
						//PS2Controller.keyboard()->setLayout(&fabgl::GermanLayout);
						break;
					case 3:				// Italian Layout
						//PS2Controller.keyboard()->setLayout(&fabgl::ItalianLayout);
						break;
					case 4:				// Spanish Layout
						//PS2Controller.keyboard()->setLayout(&fabgl::SpanishLayout);
						break;
					case 5:				// French Layout
						//PS2Controller.keyboard()->setLayout(&fabgl::FrenchLayout);
						break;
					case 6:				// Belgian Layout
						//PS2Controller.keyboard()->setLayout(&fabgl::BelgianLayout);
						break;
					case 7:				// Norwegian Layout
						//PS2Controller.keyboard()->setLayout(&fabgl::NorwegianLayout);
						break;
					case 8:				// Japanese Layout
						//PS2Controller.keyboard()->setLayout(&fabgl::JapaneseLayout);
						break;
					default:
						//PS2Controller.keyboard()->setLayout(&fabgl::UKLayout);
						break;
					}
				}
			break;
		case PACKET_CURSOR: 	// VDU 23, 0, 2
			// TODO
			//sendCursorPosition();	// Send cursor position
			vdp_unqueue_input(3);
			break;
		case PACKET_SCRCHAR: 		// VDU 23, 0, 3, x; y;
			if (vdp_serial_input_queue_len >= 7)
				{
				//word x = readWord();	// Get character at screen position x, y
				//word y = readWord();
				//sendScreenChar(x, y);
				vdp_unqueue_input(7);
				}
			break;
		case PACKET_SCRPIXEL: 		// VDU 23, 0, 4, x; y;
			if (vdp_serial_input_queue_len >= 7)
				{
				//word x = readWord();	// Get pixel value at screen position x, y
				//word y = readWord();
				//sendScreenPixel(x, y);
				vdp_unqueue_input(7);
				}
			break;		
		case PACKET_AUDIO: 		// VDU 23, 0, 5, channel, waveform, volume, freq; duration;
			if (vdp_serial_input_queue_len >= 10)
				{
				//byte channel = readByte();
				//byte waveform = readByte();
				//byte volume = readByte();
				//word frequency = readWord();
				//word duration = readWord();
				//word success = play_note(channel, volume, frequency, duration);
				//sendPlayNote(channel, success);
				vdp_unqueue_input(10);
				}
			break;
		case PACKET_MODE: 			// VDU 23, 0, 6
			// TODO
			//sendModeInformation();	// Send mode information (screen dimensions, etc)
			vdp_unqueue_input(3);
			break;
  	}
}


/// Handle VDU 29
void vdu_origin() {
	// TODO
	//origin.X = readWord();
	//origin.Y = readWord();
	//debug_log("vdu_origin: %d,%d\n\r", origin.X, origin.Y);
}

/// Draw a character at the current cursor position
/// @param c 
void drawChar(uint8_t c)
{
    if (c == '\n')
        {
        VDP_State.cursorX = 0;
        cursorDown();
				return;
        }

		// Limit to allowable character range, then get offset into CS data
		if (c < 0x20 || c > 127)
			c = '?';

		SDL_SetRenderDrawColor(sdlRenderer, 255, 0, 0, SDL_ALPHA_OPAQUE);
		int charDataOffset = (c - 0x20) * 8;
		for (int y = 0; y < CHARHEIGHT; y++)
			{
			uint8_t d = FONT_AGON_BITMAP[charDataOffset + y];
			for (int x = 0; x < CHARWIDTH; x++)
				{
				if (d & 0x80)
					SDL_RenderDrawPoint(sdlRenderer, VDP_State.cursorX * CHARWIDTH + x, VDP_State.cursorY * CHARHEIGHT + y);
				d <<= 1;
				}
			}

		//SDL_RenderDrawPoint(sdlRenderer, VDP_State.cursorX * 8, VDP_State.cursorY * 8);
		//SDL_RenderDrawLine(sdlRenderer, 0, 0, VDP_State.cursorX * 8, VDP_State.cursorY * 8);
		SDL_RenderPresent(sdlRenderer);
		// Update the window display
		SDL_UpdateWindowSurface( sdlWindow );

    screenBufferText[VDP_State.cursorY][VDP_State.cursorX] = c;
    drawTextScreen();
}

void drawString(char *s)
{
  for (int i = 0; i < (int)strlen(s); i++)
      {
		 	drawChar(s[i]);
      VDP_State.cursorX++; 
      }
}


/// Handle a VDU command from the serial port
void handle_VDU_command()
{
	if (0 == vdp_serial_input_queue_len)
		return;

	// Handle single-char putc
	uint8_t c = vdp_serial_input_buffer[0];
	if(c >= 0x20 && c != 0x7F)
		{
		//Canvas->setPenColor(tfg);
		//Canvas->setBrushColor(tbg);
		//Canvas->drawTextFmt(charX, charY, "%c", c);
		drawChar(c);
		VDP_State.cursorX++;
		vdp_unqueue_input(1);
		return;
		}

	// Handle other commands
	switch(c)
		{
		case 0x08:  // Cursor Left
			cursorLeft(); vdp_unqueue_input(1);
			break;
		case 0x09:  // Cursor Right
			cursorRight(); vdp_unqueue_input(1);
			break;
		case 0x0A:  // Cursor Down
			cursorDown(); vdp_unqueue_input(1);
			break;
		case 0x0B:  // Cursor Up
			cursorUp(); vdp_unqueue_input(1);
			break;
		case 0x0C:  // CLS
			cls(); vdp_unqueue_input(1);
			break;
		case 0x0D:  // CR
			cursorHome(); vdp_unqueue_input(1);
			break;
		case 0x10:	// CLG
			//clg();
			break;
		case 0x11:	// COLOUR
			//vdu_colour();
			break;
		case 0x12:  // GCOL
			//vdu_gcol();
			break;
		case 0x13:	// Define Logical Colour
			//vdu_palette();
			break;
		case 0x16:  // Mode
			if (vdp_serial_input_queue_len > 1)
				{
				vdu_mode(vdp_serial_input_buffer[1]);
				vdp_unqueue_input(2);
				}
			break;
		case 0x17:  // VDU 23
			if (vdp_serial_input_queue_len > 1)
				{
				vdu_sys();							// This does it's own unqueing
				}
			break;
		case 0x19:  // PLOT
			// TODO
			//vdu_plot();
			break;
		case 0x1D:	// VDU_29
			if (vdp_serial_input_queue_len > 4)
			{
			vdu_origin();
			vdp_unqueue_input(5);
			}
			break;
		case 0x1E:  // Home
			cursorHome(); vdp_unqueue_input(1);
			break;
		case 0x1F:	// TAB(X,Y)  - JH - NOT REALLY A TAB, MORE LIKE A SETPOS
			if (vdp_serial_input_queue_len > 2)
				{
				cursorTab(vdp_serial_input_buffer[1], vdp_serial_input_buffer[2]);
				vdp_unqueue_input(3);
				}
			break;
		case 0x7F:  // Backspace
			cursorLeft();
			drawChar(VDP_State.cursorX, VDP_State.cursorY, " ");
			vdp_unqueue_input(1);
			break;
		default :		// Unhandled VDU code
			printf("Unhandled VDU code %d\n", c);
			vdp_unqueue_input(1);
			break;
		}



}

/// Give the VDP a chance to do some processing
void vdp_tick()
{
	// As in video.ino loop():
	// - Flash cursor

	// - Read keyboard

	// - Read serial command stream
	if (vdp_serial_input_queue_len > 0)
		{
		// See if we can process the VDU command
		handle_VDU_command();
		}

}

