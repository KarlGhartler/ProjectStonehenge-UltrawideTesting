# SLUS_201.52 ELF sections

Source: Ghidra memory map (Elf Loader + ghidra-emotionengine-reloaded).

## Loaded sections

| Section | Start | End | Size | Flags | Notes |
|---|---|---|---|---|---|
| `.text` | 0x00100000 | 0x00297B33 | 0x197B34 | R X | Game code + Sony SDK + libc/libgcc |
| `.vutext` | 0x00297C00 | 0x0029C8FF | 0x4D00 | R X | VU microcode (~2,464 64-bit instructions); not R5900 |
| `.data` | 0x0029C980 | 0x004021E7 | 0x165868 | RW | ~1.4 MB, unexplored |
| `.vudata` | 0x004021F0 | 0x004036EF | 0x1500 | RW | VU data |
| `.rodata` | 0x00403700 | 0x0040D7D7 | 0xA0D8 | R | Constants, strings |
| `.gcc_except_table` | 0x0040D800 | 0x0040D9CF | 0x1D0 | RW | GCC C++ exception tables |
| `.lit4` | 0x0040DA00 | 0x0041046F | 0x2A70 | RW | ~2,716 float literals (GCC/MIPS) |
| `.sdata` | 0x00410480 | 0x0041172B | 0x12AC | RW | gp-relative small data |
| `.sbss` | 0x00411780 | 0x0041193B | 0x1BC | RW | gp-relative small zero-init data |
| `.bss` | 0x00411980 | 0x004CC74B | 0xBADCC | RW | ~765 KB zero-init data |

## Not-loaded sections

| Section | File offset | Size | Notes |
|---|---|---|---|
| `.reginfo` | 0x3127C4 | 0x18 | MIPS register info; `ri_gp_value` at offset 0x14 |
| `.DVP.ovlytab` | 0x3127DC | 0xCC | VU overlay table |
| `.DVP.ovlystrtab` | 0x3128A8 | 0x22C | VU overlay names |
| `.DVP.overlay.*` (17 sections) | 0x312AD4–0x318A8B | 0x60–0x800 each | VU microcode overlays; names contain `0x0` or `unknvma` as VU load address |
| `.shstrtab` | 0x318A8C | 0x2BD | Section names |

## Absent sections (meaning)

- `.symtab`, `.strtab`: stripped, no symbol names.
- `.mdebug`: no STABS debug info.
- `.comment`: no compiler version string.
- `.lit8`: no double-precision literals.
