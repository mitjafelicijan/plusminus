CC           ?= cc
CFLAGS       := -std=c99 -pedantic -Wall -Wextra -Wunused -Wswitch-enum
CPPFLAGS     := $(shell pkg-config --cflags xft)
LDFLAGS      := $(shell pkg-config --libs x11 xft)
DISPLAY_NUM  := 69

plusminus: main.c logging.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ $^ $(LDFLAGS)

virt:
	Xephyr -screen 1000x1000 :$(DISPLAY_NUM)
