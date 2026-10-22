#!/usr/bin/env python3
"""List the import/export tables of PS2 IOP modules (IRX) for research.

Usage:
    python tools/irx_imports.py <ExtractedGameFiles dir> [IRX name ...] [-o out/irx]

Reads <dir>/IRX/*.IRX (read-only), finds the loadcore import tables (magic 0x41E00000) and
export tables (magic 0x41C00000) in the loaded image, and writes one CSV per module to the
output directory (default out/irx/) plus a summary on stdout. It does not resolve function
names: indices are per library; names come from research notes (docs/subsystems/sound.md).

IRX layout used here: ELF32 little-endian MIPS, e_type 0xFF80 (relocatable IOP module);
program header 0 is the .iopmod info, program header 1 the loadable image at address 0.
Import stub = `jr ra` (0x03E00008) + `addiu zero, zero, index` (0x2400xxxx).
"""
import argparse
import csv
import os
import struct
import sys

IMPORT_MAGIC = 0x41E00000
EXPORT_MAGIC = 0x41C00000


def load_image(path):
    data = open(path, "rb").read()
    if data[:4] != b"\x7fELF":
        raise ValueError(f"{path}: not an ELF file")
    e_type, e_machine = struct.unpack_from("<HH", data, 16)
    phoff, = struct.unpack_from("<I", data, 28)
    phentsize, phnum = struct.unpack_from("<HH", data, 42)
    image = bytearray()
    for i in range(phnum):
        p_type, p_off, p_vaddr, _, p_filesz, p_memsz = struct.unpack_from("<6I", data, phoff + i * phentsize)
        if p_type == 1:  # PT_LOAD
            if len(image) < p_vaddr + p_memsz:
                image.extend(b"\0" * (p_vaddr + p_memsz - len(image)))
            image[p_vaddr:p_vaddr + p_filesz] = data[p_off:p_off + p_filesz]
    return e_type, e_machine, bytes(image)


def name8(image, off):
    return image[off:off + 8].split(b"\0")[0].decode("ascii", "replace")


def scan(image):
    imports, exports = [], []
    for off in range(0, len(image) - 20, 4):
        magic, = struct.unpack_from("<I", image, off)
        if magic == IMPORT_MAGIC:
            version, = struct.unpack_from("<H", image, off + 8)
            lib = name8(image, off + 12)
            p = off + 20
            while p + 8 <= len(image):
                w0, w1 = struct.unpack_from("<II", image, p)
                if w0 != 0x03E00008 or (w1 & 0xFFFF0000) != 0x24000000:
                    break
                imports.append((lib, version, w1 & 0xFFFF, p))
                p += 8
        elif magic == EXPORT_MAGIC:
            version, = struct.unpack_from("<H", image, off + 8)
            lib = name8(image, off + 12)
            p, idx = off + 20, 0
            while p + 4 <= len(image):
                ptr, = struct.unpack_from("<I", image, p)
                if ptr == 0 or ptr >= len(image):
                    break
                exports.append((lib, version, idx, ptr))
                p += 4
                idx += 1
    return imports, exports


def read_iopmod(path):
    """Parse the .iopmod section: {moduleinfo, entry, gp, text_size, data_size, bss_size, u16 version, name}."""
    data = open(path, "rb").read()
    shoff, = struct.unpack_from("<I", data, 32)
    shentsize, shnum, shstrndx = struct.unpack_from("<HHH", data, 46)
    for i in range(shnum):
        s = struct.unpack_from("<10I", data, shoff + i * shentsize)
        if s[1] == 0x70000080:  # SHT_IOPMOD
            mi, entry, gp, text, dat, bss = struct.unpack_from("<6I", data, s[4])
            version, = struct.unpack_from("<H", data, s[4] + 24)
            name = data[s[4] + 26:s[4] + s[5]].split(b"\0")[0].decode("ascii", "replace")
            return {"entry": entry, "gp": gp, "text": text, "data": dat, "bss": bss, "version": version, "name": name}
    return None


def read_symbols(path):
    """Return [(name, value, size, type, bind, shndx)] from the ELF .symtab (IRX files keep it)."""
    data = open(path, "rb").read()
    shoff, = struct.unpack_from("<I", data, 32)
    shentsize, shnum, shstrndx = struct.unpack_from("<HHH", data, 46)
    secs = [struct.unpack_from("<10I", data, shoff + i * shentsize) for i in range(shnum)]
    out = []
    for s in secs:
        if s[1] != 2:  # SHT_SYMTAB
            continue
        strtab = secs[s[6]]
        for off in range(s[4], s[4] + s[5], 16):
            st_name, st_value, st_size, st_info, st_other, st_shndx = struct.unpack_from("<IIIBBH", data, off)
            name = data[strtab[4] + st_name:].split(b"\0")[0].decode("ascii", "replace")
            if name:
                out.append((name, st_value, st_size, st_info & 15, st_info >> 4, st_shndx))
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("game_dir", help="ExtractedGameFiles directory (contains IRX/)")
    ap.add_argument("modules", nargs="*", help="IRX file names (default: all)")
    ap.add_argument("-o", "--out", default=os.path.join("out", "irx"))
    ap.add_argument("--image", action="store_true", help="also write <name>.image.bin (loaded image at address 0)")
    args = ap.parse_args()
    irx_dir = os.path.join(args.game_dir, "IRX")
    names = args.modules or sorted(f for f in os.listdir(irx_dir) if f.upper().endswith(".IRX"))
    os.makedirs(args.out, exist_ok=True)
    for name in names:
        e_type, e_machine, image = load_image(os.path.join(irx_dir, name))
        imports, exports = scan(image)
        libs = {}
        for lib, _, idx, _ in imports:
            libs.setdefault(lib, []).append(idx)
        print(f"== {name}: e_type {e_type:#06x}, e_machine {e_machine}, image {len(image):#x} bytes, "
              f"{len(imports)} imports from {len(libs)} libraries, {len(exports)} exports")
        for lib, idxs in libs.items():
            print(f"   {lib:10s} {sorted(idxs)}")
        if args.image:
            # Loadable image at its link address 0 (IRX code is already relocated for base 0), for a raw
            # Ghidra import with language MIPS:LE:32:default. Written to out/ only (derived game data).
            with open(os.path.join(args.out, f"{name}.image.bin"), "wb") as fh:
                fh.write(image)
            iopmod = read_iopmod(os.path.join(irx_dir, name))
            if iopmod:
                print(f"   .iopmod: entry {iopmod['entry']:#x}, gp {iopmod['gp']:#x}, text {iopmod['text']:#x}, "
                      f"data {iopmod['data']:#x}, bss {iopmod['bss']:#x}, version {iopmod['version']:#x}, name {iopmod['name']!r}")
        syms = read_symbols(os.path.join(irx_dir, name))
        funcs = [s for s in syms if s[3] == 2]
        print(f"   .symtab: {len(syms)} named symbols, {len(funcs)} functions")
        with open(os.path.join(args.out, f"{name}.csv"), "w", newline="") as fh:
            w = csv.writer(fh)
            w.writerow(["kind", "library", "version", "index", "address"])
            for lib, ver, idx, addr in imports:
                w.writerow(["import", lib, f"{ver:#x}", idx, f"{addr:#010x}"])
            for lib, ver, idx, addr in exports:
                w.writerow(["export", lib, f"{ver:#x}", idx, f"{addr:#010x}"])
        with open(os.path.join(args.out, f"{name}.symbols.csv"), "w", newline="") as fh:
            w = csv.writer(fh)
            w.writerow(["name", "address", "size", "type", "bind", "section"])
            for n, v, sz, t, b, sh in syms:
                w.writerow([n, f"{v:#010x}", sz, {0: "notype", 1: "object", 2: "func", 3: "section", 4: "file"}.get(t, t),
                            {0: "local", 1: "global", 2: "weak"}.get(b, b), sh])
    return 0


if __name__ == "__main__":
    sys.exit(main())
