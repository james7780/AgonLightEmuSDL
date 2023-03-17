CC    = clang
RM    = rm -f
RMDIR = rmdir

BUILDDIR = ./build
# If you want debug/symbol info, add -gX and remove -OX as needed
# If you want core debug support, add -DDEBUG_SUPPORT
# If you want the emulator to run on a different thread than the gui, add -DMULTITHREAD
# CFLAGS = -Wall -Wextra -fPIC -O3 -std=gnu11 -static
CFLAGS = -Wall -Wextra -fPIC -g -O0 -std=gnu11 -static

# Add debugging support, with zdis disassembler
CFLAGS += -DDEBUG_SUPPORT

INCLUDE_DIRS = ./emu-library ./emu-library/debug/zdis ./IHex-library
LIBRARIES 	 = libcemucore.a libihex.a
OBJECTS   	 = main.o utils.o agon_vdp.o

OBJS = $(patsubst %.o, $(BUILDDIR)/%.o, $(OBJECTS))
LIBS = $(patsubst %.a, $(BUILDDIR)/%.a, $(LIBRARIES))

eZ80_emu: $(OBJS)
	$(MAKE) -C ./emu-library Makefile all
	$(MAKE) -C ./IHex-library Makefile all
	$(CC) $(CFLAGS) -o $@ $(OBJS) -Wl $(LIBS)  

$(BUILDDIR)/%.o: %.c $(INCLUDE_DIRS)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(INCLUDE_DIRS:%=-I %) -c -o $@ $<

clean:
	$(MAKE) -C ./emu-library Makefile clean
	$(MAKE) -C ./IHex-library Makefile clean
	$(RM) $(OBJS) eZ80_emu
	$(RMDIR) $(BUILDDIR)/debug/zdis
	$(RMDIR) $(BUILDDIR)/debug
	$(RMDIR) $(BUILDDIR)/os
	$(RMDIR) $(BUILDDIR)/usb
	$(RMDIR) $(BUILDDIR)
	
.PHONY: clean eZ80_emu
