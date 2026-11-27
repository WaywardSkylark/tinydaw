#!/usr/bin/env python3
"""Puts tinydaw.exe, the DLLs it needs, a font and the licences into win/dist and zips them.

The DLLs are found by reading the import tables of the program and every DLL it pulls in;
anything not in the MSYS2 sysroot (kernel32, user32, ...) is part of Windows.
usage: python3 -I win/package.py [VERSION]   (default: the VERSION file)
"""
import os
import shutil
import sys
import subprocess
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
BIN = os.path.join(HERE, "sysroot", "clang64", "bin")
OBJDUMP = os.path.expanduser("~/.local/opt/llvm-mingw/bin/llvm-objdump")
VERSION = sys.argv[1] if len(sys.argv) > 1 else open(os.path.join(ROOT, "VERSION")).read().strip()
NAME = f"tinydaw-{VERSION}-windows-x64"
OUT = os.path.join(HERE, "dist", NAME)
FONT = "/usr/share/fonts/TTF/DejaVuSansMono.ttf"
FONT_LICENSE = "/usr/share/licenses/ttf-dejavu/LICENSE"


def imports(path):
    out = subprocess.run([OBJDUMP, "-p", path], capture_output=True, text=True, check=True).stdout
    return [line.split(":", 1)[1].strip() for line in out.splitlines() if "DLL Name:" in line]


def bundle_espeak():
    """eSpeak NG, the speech program the Singer runs, in espeak-ng/ with its own DLLs and voice data.

    It's a separate program (GPL-3), shipped alongside tinydaw rather than built into it."""
    exe = os.path.join(BIN, "espeak-ng.exe")
    if not os.path.exists(exe):
        print("espeak-ng not in the sysroot (run win/fetch-deps.py): the Singer will ask for it")
        return
    local = {f.lower(): f for f in os.listdir(BIN) if f.lower().endswith(".dll")}
    dest = os.path.join(OUT, "espeak-ng", "bin")
    os.makedirs(dest)
    shutil.copy2(exe, dest)
    need, todo = set(), [exe]
    while todo:
        for dll in imports(todo.pop()):
            real = local.get(dll.lower())
            if real and real not in need:
                need.add(real)
                todo.append(os.path.join(BIN, real))
    for dll in sorted(need):
        shutil.copy2(os.path.join(BIN, dll), dest)
    shutil.copytree(os.path.join(BIN, "..", "share", "espeak-ng-data"),
                    os.path.join(OUT, "espeak-ng", "share", "espeak-ng-data"))
    with open(os.path.join(OUT, "espeak-ng", "README.txt"), "w", newline="\r\n") as f:
        f.write("eSpeak NG, the free speech synthesizer that tinydaw's Singer uses to say the words.\n\n"
                "It is a separate program, distributed here unchanged under the GNU General Public\n"
                "License version 3 (see licenses/espeak-ng and licenses/pcaudiolib next to tinydaw.exe).\n"
                "Its source code: https://github.com/espeak-ng/espeak-ng\n"
                "This build comes from MSYS2: https://packages.msys2.org/base/mingw-w64-espeak-ng\n\n"
                "To use a different copy, delete this folder and install eSpeak NG, or point the\n"
                "TINYDAW_ESPEAK environment variable at its espeak-ng.exe.\n")
    print(f"eSpeak NG bundled with {len(need)} DLLs")


def main():
    local = {f.lower(): f for f in os.listdir(BIN) if f.lower().endswith(".dll")}
    exe = os.path.join(HERE, "build", "tinydaw.exe")
    need, todo = set(), [exe]
    # loaded at run time rather than imported, so the import tables don't show them
    LOADED = {"SDL2.dll": ["SDL3.dll"]}
    while todo:
        for dll in imports(todo.pop()):
            real = local.get(dll.lower())
            if real and real not in need:
                need.add(real)
                todo.append(os.path.join(BIN, real))
                for extra in LOADED.get(real, []):
                    if extra not in need and extra.lower() in local:
                        need.add(local[extra.lower()])
                        todo.append(os.path.join(BIN, local[extra.lower()]))
    dist = os.path.join(HERE, "dist")
    if os.path.isdir(dist):                      # only the current version's folder and zip
        shutil.rmtree(dist)
    os.makedirs(os.path.join(OUT, "licenses"))
    shutil.copy2(exe, OUT)
    for dll in sorted(need):
        shutil.copy2(os.path.join(BIN, dll), OUT)
    shutil.copy2(FONT, OUT)
    shutil.copy2(FONT_LICENSE, os.path.join(OUT, "licenses", "DejaVu-fonts.txt"))
    lic = os.path.join(HERE, "sysroot", "clang64", "share", "licenses")
    if os.path.isdir(lic):
        for pkg in sorted(os.listdir(lic)):
            shutil.copytree(os.path.join(lic, pkg), os.path.join(OUT, "licenses", pkg))
    os.makedirs(os.path.join(OUT, "examples"))          # the example songs (Menu > Open an example song)
    for f in sorted(os.listdir(os.path.join(ROOT, "examples"))):
        if f.endswith(".tdaw"):
            shutil.copy2(os.path.join(ROOT, "examples", f), os.path.join(OUT, "examples"))
    bundle_espeak()
    shutil.copy2(os.path.join(ROOT, "README.md"), OUT)
    shutil.copy2(os.path.join(HERE, "README-windows.txt"), OUT)
    zpath = os.path.join(HERE, "dist", NAME + ".zip")
    with zipfile.ZipFile(zpath, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for dirpath, _, files in os.walk(OUT):
            for f in files:
                full = os.path.join(dirpath, f)
                z.write(full, os.path.join(NAME, os.path.relpath(full, OUT)))
    size = sum(os.path.getsize(os.path.join(OUT, f)) for f in os.listdir(OUT) if os.path.isfile(os.path.join(OUT, f)))
    print(f"{len(need)} DLLs: {', '.join(sorted(need))}")
    print(f"{OUT} ({size / 1e6:.1f} MB), {zpath} ({os.path.getsize(zpath) / 1e6:.1f} MB)")


if __name__ == "__main__":
    main()
