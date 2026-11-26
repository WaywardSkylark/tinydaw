CC      ?= cc
CFLAGS  ?= -O2 -g
CFLAGS  += -std=gnu11 -Wall -Wextra
PKGS     = sdl2 SDL2_ttf sndfile samplerate
# MIDI input uses the ALSA sequencer when its headers are installed
ifeq ($(shell pkg-config --exists alsa && echo yes),yes)
PKGS    += alsa
CFLAGS  += -DHAVE_ALSA
endif
CFLAGS  += $(shell pkg-config --cflags $(PKGS))
LDLIBS   = $(shell pkg-config --libs $(PKGS)) -lm -lpthread

SRC = src/main.c src/audio.c src/theory.c src/project.c src/midi.c
HDR = src/daw.h src/theory.h src/midi.h

tinydaw: $(SRC) $(HDR)
	$(CC) $(CFLAGS) -o $@ $(SRC) $(LDLIBS)

clean:
	rm -f tinydaw

.PHONY: clean
