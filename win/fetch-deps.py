#!/usr/bin/env python3
"""Downloads the Windows builds of tinydaw's libraries from MSYS2 (clang64) into win/sysroot.

Resolves dependencies from the repository database and checks every package's SHA-256.
Needs Python 3.14+ (for zstd). usage: python3 -I win/fetch-deps.py
"""
import hashlib
import io
import os
import shutil
import sys
import tarfile
import urllib.request

REPO = "https://repo.msys2.org/mingw/clang64/"
PREFIX = "mingw-w64-clang-x86_64-"
ROOTS = ["SDL2", "SDL2_ttf", "libsndfile", "libsamplerate"]
HERE = os.path.dirname(os.path.abspath(__file__))
CACHE = os.path.join(HERE, "cache")
SYSROOT = os.path.join(HERE, "sysroot")


def fetch(name):
    os.makedirs(CACHE, exist_ok=True)
    path = os.path.join(CACHE, name)
    if not os.path.exists(path):
        with urllib.request.urlopen(REPO + name) as r, open(path + ".part", "wb") as f:
            shutil.copyfileobj(r, f)
        os.replace(path + ".part", path)
    return path


def read_db():
    data = open(fetch("clang64.db"), "rb").read()
    pkgs, provides = {}, {}
    with tarfile.open(fileobj=io.BytesIO(data), mode="r:*") as t:
        for m in t.getmembers():
            if not m.name.endswith("/desc"):
                continue
            fields, key = {}, None
            for line in t.extractfile(m).read().decode().splitlines():
                if line.startswith("%") and line.endswith("%"):
                    key = line.strip("%")
                    fields[key] = []
                elif line and key:
                    fields[key].append(line)
            name = fields["NAME"][0]
            pkgs[name] = fields
            for p in fields.get("PROVIDES", []):
                provides[p.split("=")[0]] = name
    return pkgs, provides


def strip_version(dep):
    for op in (">=", "<=", "=", ">", "<"):
        dep = dep.split(op)[0]
    return dep


def main():
    pkgs, provides = read_db()
    want, todo = [], [PREFIX + r for r in ROOTS]
    while todo:
        name = strip_version(todo.pop())
        name = name if name in pkgs else provides.get(name, name)
        if name in want or name not in pkgs:
            continue
        want.append(name)
        todo += pkgs[name].get("DEPENDS", [])
    if os.path.isdir(SYSROOT):
        shutil.rmtree(SYSROOT)
    os.makedirs(SYSROOT)
    for name in sorted(want):
        desc = pkgs[name]
        fname, sha = desc["FILENAME"][0], desc["SHA256SUM"][0]
        path = fetch(fname)
        if hashlib.sha256(open(path, "rb").read()).hexdigest() != sha:
            os.remove(path)
            sys.exit(f"checksum mismatch for {fname}")
        with tarfile.open(path, mode="r:*") as t:
            members = [m for m in t.getmembers() if m.name.startswith("clang64/")]
            t.extractall(SYSROOT, members=members, filter="data")
        print(f"  {desc['VERSION'][0]:<14} {name[len(PREFIX):]}")
    print(f"{len(want)} packages in {SYSROOT}/clang64")


if __name__ == "__main__":
    main()
