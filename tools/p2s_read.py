"""Read memory from a PCSX2 save state (.p2s) for research.

A .p2s file is a zip archive. PCSX2 2.8.2 with compression set to "Uncompressed"
stores (stdlib zipfile can read stored and deflated entries, not zstd):
    eeMemory.bin   32 MiB EE RAM        iopMemory.bin  2 MiB IOP RAM
    Scratchpad.bin 16 KiB (0x70000000)   eeHwRegs.bin / iopHwRegs.bin  64 KiB each
    vu0/vu1Memory.bin, vu0/vu1MicroMem.bin, SPU2.bin, GS.bin, PAD.bin, USB.bin
    "PCSX2 Internal Structures.dat"  CPU registers etc. (version-specific, not parsed)

Save states contain game memory: keep them outside the repo, and write output only
to out/ (CLAUDE.md, "Game files").

Usage:
    p2s_read.py STATE.p2s hex  ADDR [LEN]        hex dump (EE address; 0x7000xxxx = scratchpad;
                                                 prefix iop: for IOP RAM, e.g. iop:0x1000)
    p2s_read.py STATE.p2s u32  ADDR [COUNT]      little-endian words
    p2s_read.py STATE.p2s find HEXBYTES [ENTRY]  offsets of a byte pattern in an entry (default eeMemory.bin)
    p2s_read.py STATE.p2s dump ADDR LEN OUTFILE  raw bytes to a file under out/
    p2s_read.py STATE.p2s list                   zip entries
"""
import os
import struct
import sys
import zipfile

EE_RAM = 'eeMemory.bin'
IOP_RAM = 'iopMemory.bin'
SPR = 'Scratchpad.bin'


class State:
    def __init__(self, path):
        self.zip = zipfile.ZipFile(path)
        self.cache = {}

    def entry(self, name):
        if name not in self.cache:
            self.cache[name] = self.zip.read(name)
        return self.cache[name]

    def resolve(self, addr_text):
        """Map an address string to (entry name, offset)."""
        if addr_text.lower().startswith('iop:'):
            return IOP_RAM, int(addr_text[4:], 0) & 0x1FFFFF
        a = int(addr_text, 0)
        if (a & 0xFFFFC000) == 0x70000000:
            return SPR, a & 0x3FFF
        # KSEG0/KSEG1 and the uncached aliases 0x2xxxxxxx / 0x3xxxxxxx map to physical RAM
        return EE_RAM, a & 0x01FFFFFF

    def read(self, addr_text, length):
        name, off = self.resolve(addr_text)
        data = self.entry(name)
        if off + length > len(data):
            raise SystemExit(f'read past end of {name}: 0x{off:X}+0x{length:X}')
        return data[off:off + length]


def hexdump(base, data):
    for i in range(0, len(data), 16):
        row = data[i:i + 16]
        print(f'{base + i:08X}  ' + ' '.join(f'{b:02X}' for b in row))


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 2
    st = State(argv[1])
    cmd = argv[2]
    if cmd == 'list':
        for i in st.zip.infolist():
            print(f'{i.filename:40} {i.file_size:10} compress={i.compress_type}')
    elif cmd == 'hex':
        length = int(argv[4], 0) if len(argv) > 4 else 0x40
        base = int(argv[3].split(':')[-1], 0)
        hexdump(base, st.read(argv[3], length))
    elif cmd == 'u32':
        count = int(argv[4], 0) if len(argv) > 4 else 1
        base = int(argv[3].split(':')[-1], 0)
        data = st.read(argv[3], 4 * count)
        for i in range(count):
            print(f'{base + 4 * i:08X}  0x{struct.unpack_from("<I", data, 4 * i)[0]:08X}')
    elif cmd == 'find':
        pat = bytes.fromhex(argv[3])
        data = st.entry(argv[4] if len(argv) > 4 else EE_RAM)
        pos = data.find(pat)
        while pos >= 0:
            print(f'0x{pos:08X}')
            pos = data.find(pat, pos + 1)
    elif cmd == 'dump':
        out = os.path.abspath(argv[5])
        if os.sep + 'out' + os.sep not in out + os.sep:
            raise SystemExit('dump output must be under out/')
        os.makedirs(os.path.dirname(out), exist_ok=True)
        with open(out, 'wb') as f:
            f.write(st.read(argv[3], int(argv[4], 0)))
    else:
        print(__doc__)
        return 2
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
