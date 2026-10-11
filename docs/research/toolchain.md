# Toolchain of SLUS_201.52

Evidence comes from scanning the loaded segment of the ELF for strings and from the ELF headers. Scratch scripts are in `out/scratch/`, which is not committed.

## gp register

| Source | Value |
|---|---|
| `.reginfo` `ri_gp_value` (file 0x3127C4 + 0x14) | `0x004159F0` |
| `entry` @ 0x00100048–0x0010005C: `lui a0,0x41; addiu a0,a0,0x59f0; move gp,a0` | `0x004159F0` |
| Ghidra: `lw v1,-0x5548(gp)` @ 0x00100248 resolves to `DAT_004104a8` (`.sdata`) | consistent with `0x004159F0` |

Conclusion: Ghidra's `gp` is correct. Since 2026-10-08 it is set as one register-context range (`gp_qw` = 0x004159F0) over 0x00100000–0x00297B30, and the decompiler resolves all sampled gp-relative globals. The only instruction in the program that writes `gp` is crt0 `entry` @ 0x0010005C (`open-questions.md`). `.reginfo` `gprmask` = 0xA0000074. The gp window covers 0x0040D9F0–0x0041D9EF, which includes `.lit4`, `.sdata` and `.sbss`.

## Sony SDK version: Runtime Library 2.3.x

Library ID strings (`PsII` + 8-char library name + 4-digit version):

| Address | Library | Version |
|---|---|---|
| 0x003FAA90 | libgraph | 2300 |
| 0x003FAB00 | libdma | 2300 |
| 0x003FAB80 | libpkt | 2300 |
| 0x003FABC0 | libkernl | 2300 |
| 0x003FB440 | libpad | 2300 |
| 0x003FB488 | libmc | **2320** |
| 0x003FB4C0 | libcdvd | 2300 |
| 0x003FCF30 | libpc | 2300 |
| 0x003FCF40 | libmpeg | 2300 |
| 0x003FD950 | libipu | 2300 |

- Most libraries are from SDK **2.3.0**. libmc is from **2.3.2**, probably a later drop-in update.
- libpc is the performance counter library. Linking libmpeg and libipu means FMV is decoded on the EE with the IPU. These are probably the `SMV/*.SMV` files; unverified.
- Some libraries carry no `PsII` tag, so the list above is not complete. The SIF syscalls are used, so sifcmd/sifrpc are surely linked. libvu0, libdev and libsdr are unverified; step C will check them from code.
- Other SDK strings: `## internel error in libkernl.a!`, `[libpad.a = %d.%d, padman.irx = %d.%d]`, `libmc: too old release of mcserv.irx/mcman.irx`, `sceGsSyncPath: ... does not terminate`, `rom0:UDNL `.
- IOP module load list at 0x00403710: `cdrom0:\IOPRP23.IMG;1`, then SIO2MAN, MCMAN, MCSERV, PADMAN, LIBSD, SOUND, VAG, USBD, USBJOY. `IOPRP23` matches the 2.3 SDK.
- `host0:%s` (0x00403B10) and `host0:` (0x00410C80, 0x00410DB0) are leftover dev-kit host file paths.

## Compiler: GCC 2.9x `ee-gcc` with g++ and libio-based libstdc++

- No `.comment` section and no `GCC:`/`GNU` version string. The exact version is unknown.
- GCC 2.x RTTI and exception type-name strings, length-prefixed with the old (pre-3.x ABI) mangling, at 0x0040B9A0–0x0040D2D8: `9type_info`, `17__class_type_info`, `14__si_type_info`, `16__user_type_info`, `17__array_type_info`, `16__ptmd_type_info`, `16__ptmf_type_info`, `16__func_type_info`, `19__builtin_type_info`, `16__attr_type_info`, `19__pointer_type_info`, `9exception`, `13bad_exception`, `9bad_alloc`, `10bad_typeid`, `8bad_cast`.
  - `__user_type_info`, `__attr_type_info` and `__ptmd/__ptmf_type_info` exist only in the GCC 2.8–2.95 runtime (`tinfo.cc`/`tinfo2.cc`), not in GCC 3+. That is consistent with SDK 2.3's `ee-gcc 2.9-ee-991111`. The version is a hypothesis.
- libio iostream classes: `22_IO_ostream_withassign`, `12ostdiostream`, `7filebuf`, `9streambuf`, `3ios`, `11_ios_fields`, `8_IO_FILE`. This is GCC 2.x libstdc++ built on libio. The iostream code is linked in, but whether the game uses it is unverified.
- No SN Systems/ProDG, Metrowerks or Cygnus/newlib strings.

## Source filenames

- None. No `.c/.cc/.cpp/.h/.s` strings exist, and no `__FILE__`-style assert messages either. Module boundaries have to come from other evidence, for example the `bad_alloc` markers below.

## C vs C++

Conclusion: game modules are compiled as C++ (g++), but written in "C with classes" style. The game has no virtual dispatch, no `new`/`delete` and no exceptions. A plain C++17 translation, with free functions or non-virtual members, is enough for the port.

| Evidence | Result |
|---|---|
| libstdc++ runtime (RTTI, exceptions, libio iostream) and `.gcc_except_table` | **Confirmed** linked |
| GCC 2.x type-info names | Only runtime/library classes. GCC 2.x emits type-info names only when they are needed (`typeid`, `dynamic_cast`, thrown types), so their absence alone does **not** prove the game has no virtual functions |
| `bad_alloc` string copies | 34 unreferenced copies in game `.rodata` (0x00403C50–0x00405A20), 8 more in libstdc++ `.rodata`. GCC 2.x emits string literals from header-defined inline functions (`bad_alloc::what()` in `<new>`) once per translation unit even when the function is unused. **HYPOTHESIS:** about 34 game TUs are compiled as C++, and the copies mark TU boundaries in `.rodata` |
| C++-style method names in game strings | `C_PCCON::start : error : no flow!`, `C_PCCON::get`, `C_PCCON::set`, `C_PCCON::stop` (functions 0x00108458, 0x00108678): the source has classes, at least by naming |
| Vtables in `.data` | The only GCC 2.x non-thunk vtable run (`{delta,index}`, `pfn` pairs) is at 0x004005D0: 17 entries pointing into libio at 0x0027Exxx. Game code has **no** vtable-shaped arrays. There are 6 game-only plain pointer tables (0x002A1620 ×31, 0x002B77B0 ×17, 0x0033DF24 ×3, 0x00384640 ×90, 0x0039DF54 ×3, 0x003B0190 ×7). They repeat the same target many times, which looks like state-machine/dispatch tables, not vtables |
| Indirect calls | Only **10** `jalr` instructions in all game code (0x00100000–0x00259000), so there is no pervasive virtual dispatch |
| `__builtin_new` @ 0x0028E4B8 (malloc + new_handler loop + throw `bad_alloc`) | **0** direct callers from game code. `__builtin_delete` @ 0x0028E440 is called only from libstdc++ |
| Game memory | Own bump allocator `func_00101000` (83 callers) over one big `malloc` block. The block size is probed by `func_00100EC8` and set up by `func_00100F48`. Game code calls `malloc`/`free` directly only from those two functions |
| GCC 2.x `__in_chrg` destructor calls (`jal` with `a1 = 2/3`) | Only 2 targets (0x0021CF70, 0x0025A058) are always called that way. Not significant |
