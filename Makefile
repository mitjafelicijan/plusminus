CC           ?= cc
CFLAGS       := -std=c99 -pedantic -Wall -Wextra -Wunused -Wswitch-enum
INCLUDES     := $(shell pkg-config --cflags xft)
LDFLAGS      := $(shell pkg-config --libs x11 xft)
DISPLAY_NUM  := 69

ifdef DEBUG
CFLAGS += -ggdb -DDEBUG
endif

ifdef OPTIMIZE
CFLAGS += -O$(OPTIMIZE)
endif

all: config.h plusminus

plusminus: main.c logging.c functions.c
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $^ $(LDFLAGS)

config.h:
	[ -f config.h ] || cp config.def.h config.h

virt:
	Xephyr -screen 1000x1000 :$(DISPLAY_NUM)
