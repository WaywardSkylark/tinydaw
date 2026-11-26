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

SRC = src/main.c src/audio.c src/fx.c src/analysis.c src/theory.c src/project.c src/midi.c
HDR = src/daw.h src/theory.h src/midi.h

tinydaw: $(SRC) $(HDR) src/compat.h
	$(CC) $(CFLAGS) -o $@ $(SRC) $(LDLIBS)

clean:
	rm -f tinydaw

.PHONY: clean

# ---- Windows, cross-compiled with llvm-mingw against MSYS2's libraries (see win/README.md)
LLVM_MINGW ?= $(HOME)/.local/opt/llvm-mingw
WINCC     = $(LLVM_MINGW)/bin/x86_64-w64-mingw32-clang
WINRC     = $(LLVM_MINGW)/bin/x86_64-w64-mingw32-windres
WINROOT   = win/sysroot/clang64
WINPC     = PKG_CONFIG_LIBDIR=$(WINROOT)/lib/pkgconfig PKG_CONFIG_SYSROOT_DIR=$(CURDIR)/win/sysroot pkg-config
WINPKGS   = sdl2 SDL2_ttf sndfile samplerate

windows: win/build/tinydaw.exe

win/build/tinydaw.exe: $(SRC) $(HDR) src/compat.h win/tinydaw.rc win/tinydaw.manifest win/tinydaw.ico
	@test -d $(WINROOT) || { echo "run: python3 -I win/fetch-deps.py"; exit 1; }
	mkdir -p win/build
	$(WINRC) -I win win/tinydaw.rc -O coff -o win/build/resources.o
	$(WINCC) -O2 -std=gnu11 -Wall -Wextra $(shell $(WINPC) --cflags $(WINPKGS)) \
	    -o $@ $(SRC) win/build/resources.o $(shell $(WINPC) --libs $(WINPKGS)) -lwinmm -lcomdlg32 -mwindows

dist-windows: windows
	python3 -I win/package.py

.PHONY: windows dist-windows
