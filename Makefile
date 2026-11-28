VERSION := $(shell cat VERSION)
# build the files side by side unless told otherwise (make -j1)
MAKEFLAGS += -j$(shell nproc 2>/dev/null || echo 4)
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
CFLAGS  += -DTINYDAW_VERSION='"$(VERSION)"'
VERNUM   = $(subst ., ,$(VERSION))

SRC = src/arrange.c src/audio_io.c src/console.c src/drums_ui.c src/edit_input.c src/editor.c src/files.c src/history.c src/live.c src/main.c src/panels.c src/roll.c src/selftest.c src/session.c src/theory_ui.c src/ui_core.c src/visual.c src/audio.c src/fx.c src/analysis.c src/scenes.c src/theory.c src/project.c src/midi.c src/synth.c src/voice.c src/midifile.c src/loudness.c src/harmony.c src/parts.c src/bend.c
HDR = src/daw.h src/theory.h src/midi.h src/scenes.h src/midifile.h src/ui.h

OBJ = $(SRC:src/%.c=build/%.o)

tinydaw: $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ) $(LDLIBS)

# each file compiled on its own: make -j builds them side by side, and an edit rebuilds only its file
build/%.o: src/%.c $(HDR) src/compat.h VERSION | build
	$(CC) $(CFLAGS) -c -o $@ $<

# the self-test is one long function: optimizing it hard only makes it slow to build
build/selftest.o: CFLAGS += -O1

build:
	mkdir -p build

clean:
	rm -rf tinydaw build

.PHONY: clean

# ---- Windows, cross-compiled with llvm-mingw against MSYS2's libraries (see win/README.md)
LLVM_MINGW ?= $(HOME)/.local/opt/llvm-mingw
WINCC     = $(LLVM_MINGW)/bin/x86_64-w64-mingw32-clang
WINRC     = $(LLVM_MINGW)/bin/x86_64-w64-mingw32-windres
WINROOT   = win/sysroot/clang64
WINPC     = PKG_CONFIG_LIBDIR=$(WINROOT)/lib/pkgconfig PKG_CONFIG_SYSROOT_DIR=$(CURDIR)/win/sysroot pkg-config
WINPKGS   = sdl2 SDL2_ttf sndfile samplerate

windows: win/build/tinydaw.exe

win/build/tinydaw.exe: $(SRC) $(HDR) src/compat.h VERSION win/tinydaw.rc win/tinydaw.manifest win/tinydaw.ico
	@test -d $(WINROOT) || { echo "run: python3 -I win/fetch-deps.py"; exit 1; }
	mkdir -p win/build
	$(WINRC) -I win -DVER_STR='\"$(VERSION)\"' -DVER_MAJ=$(word 1,$(VERNUM)) -DVER_MIN=$(word 2,$(VERNUM)) \
	    -DVER_PAT=$(word 3,$(VERNUM)) win/tinydaw.rc -O coff -o win/build/resources.o
	$(WINCC) -O2 -std=gnu11 -Wall -Wextra -DTINYDAW_VERSION='"$(VERSION)"' $(shell $(WINPC) --cflags $(WINPKGS)) \
	    -o $@ $(SRC) win/build/resources.o $(shell $(WINPC) --libs $(WINPKGS)) -lwinmm -lcomdlg32 -ladvapi32 -mwindows

dist-windows: windows
	python3 -I win/package.py $(VERSION)

.PHONY: windows dist-windows
