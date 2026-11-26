# Building tinydaw for Windows

The Windows build is cross-compiled on Linux. Nothing here needs root.

1. A compiler: [llvm-mingw](https://github.com/mstorsjo/llvm-mingw/releases), the
   `ucrt-ubuntu-*-x86_64` tarball, unpacked to `~/.local/opt/llvm-mingw`
   (or pass `LLVM_MINGW=/path` to make).
2. The libraries: `python3 -I win/fetch-deps.py` downloads MSYS2's clang64 builds of SDL2,
   SDL2_ttf, libsndfile and libsamplerate with their dependencies into `win/sysroot`
   (checksums verified against the repository database).
3. `make windows` builds `win/build/tinydaw.exe`; `make dist-windows` also collects the DLLs it
   needs (found by reading import tables), the DejaVu font and the licences into
   `win/dist/tinydaw-<version>-windows-x64.zip`, named from the `VERSION` file.

Test it with Wine (a throwaway prefix, with the Mono/Gecko install prompts turned off):

```sh
export WINEPREFIX=/tmp/tinydaw-wine WINEDLLOVERRIDES="mscoree=;mshtml="
wine win/dist/tinydaw-windows-x64/tinydaw.exe --selftest
```

What differs on Windows (`src/compat.h`, `src/midi.c`, the file dialog in `src/main.c`):
MIDI uses winmm and checks for new devices every two seconds; Open/Save use the standard
Windows dialogs; settings and autosave live in `%APPDATA%` / `%LOCALAPPDATA%`; the manifest
makes file names UTF-8; saving replaces files with `MoveFileEx`, since `rename` won't
overwrite on Windows.
