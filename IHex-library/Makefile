CC = clang
AR = ar
RM = rm -f

BUILDDIR = ../build
# If you want debug/symbol info, add -gX and remove -OX as needed
# If you want core debug support, add -DDEBUG_SUPPORT
# If you want the emulator to run on a different thread than the gui, add -DMULTITHREAD
# CFLAGS = -Wall -Wextra -fPIC -O3 -std=gnu11 -static
CFLAGS = -Wall -Wextra -fPIC -O0 -std=gnu11 -static

# Add debugging support, with zdis disassembler
# CFLAGS += -DDEBUG_SUPPORT

# Add these flags if your compiler supports it
#CFLAGS += -Wstack-protector -fstack-protector-strong --param=ssp-buffer-size=1 -fsanitize=address,bounds -fsanitize-undefined-trap-on-error

## Build rules, you shouldn't need to edit anything below this line

# source: http://blog.jgc.org/2011/07/gnu-make-recursive-wildcard-function.html
rwildcard = $(foreach d,$(wildcard $1*),$(call rwildcard,$d/,$2)$(filter $(subst *,%,$2),$d))

OBJS = $(patsubst %.c,$(BUILDDIR)/%.o,$(filter-out arm/%,$(call rwildcard,,*.c)))

STATICLIB = libihex.a

all: lib

lib: $(BUILDDIR)/$(STATICLIB)
	
$(BUILDDIR)/$(STATICLIB): $(OBJS)
	$(AR) rcs $@ $?

$(BUILDDIR)/%.o: %.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

clean:
	$(RM) $(OBJS) $(BUILDDIR)/$(STATICLIB)

.PHONY: clean all lib
