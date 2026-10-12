# Naming convention

Status: **Accepted by the owner on 2026-10-10** (the proposal below, no type prefixes). The rules are in CLAUDE.md, "Naming".

Migration (logged in `../rename-log.csv`):
- 8 globals: `g_aCdFileTable` → `g_CdFileTable`, `g_pCdFsState` → `g_CdFsState`, `g_aMovieTable` → `g_MovieTable`, `g_pGameState` → `g_GameState`, `g_wMissionNo` → `g_MissionNo`, `g_wMenuPage` → `g_MenuPage`, `g_wMenuSubPage` → `g_MenuSubPage`, `g_dwAltMissionAircraftMask` → `g_AltMissionAircraftMask`.
- `g_pMainContext` is kept: it is the allowed exception, a pointer global to the named object `g_MainContext`.
- 48 `MainContext` fields and 2 `CdFileEntry` fields use snake_case in the docs (e.g. `next_scene`, `brightness_r`). **In Ghidra they still carry Hungarian prefixes** (`wNext_scene`, `flBrightness_r`): `modify_struct_field` reports success but the plugin re-applies the prefix. Field names that start with `unk_` are kept. These fields need a manual rename in Ghidra's Structure Editor.
- Docs use the new names, including `../pcsx2/sessions-02.md`, which only quoted field names.

ghidra-mcp naming gate:
- `rename_symbol`/`set_global` print a "missing_hungarian_prefix" warning, and reject the name unless `strict_mode=false` is passed per call. All renames above used `strict_mode=false`.
- `modify_struct_field` has no visible gate, but silently re-applies the type prefix to the new field name (check with `get_struct_layout`).
- `create_struct` may still auto-prefix fields: rename them right after creating the struct.
- No program option controls the gate (checked `get_program_options`: only Decompiler, Specification Extensions, Analyzers, Program Information). A global default, if one exists, would be in the GhidraMCP plugin's own settings in Ghidra, not in this project.

## Analysis behind the decision

### Names in use before the decision (`../symbols.csv`, 321 rows + Ghidra-only names)

| Category | Style in use | Count | Examples |
|---|---|---|---|
| Game functions | PascalCase, mostly verb first | 80 (79 + `PollVblankStart`) | `RequestScene`, `LoadFlightDataEntry`, `InitGimTexture`, `SceneStep` |
| Game function hypotheses (plate comments only) | PascalCase | ~45 | `SceneStartup`, `MissionEventUpdate` |
| Sony SDK functions | original Sony names | 122 `sce*` + 41 kernel syscalls + 5 `_`-prefixed internals | `sceGsSyncV`, `CreateThread`, `_InitSys` |
| libc / libgcc | original names | 54 | `sinf`, `__adddf3` |
| Globals | `g_` + Hungarian type prefix | 9 | `g_pMainContext`, `g_wMissionNo`, `g_dwAltMissionAircraftMask`, `g_aCdFileTable` |
| Globals | `g_` + PascalCase, no type prefix | 1 | `g_MainContext` (struct instance) |
| Struct fields (Ghidra) | Hungarian, **added automatically by the ghidra-mcp plugin** | ~50 | `dwFlags`, `wScene`, `pCur_buf`, `aDbuf`, `bZbuf_psm`, `flUnk_80` |
| Struct fields (docs) | mixed: Ghidra names plus `unk_XX` | | `../game/main-context.md` |
| Types | PascalCase | 2 | `MainContext`, `CdFileEntry` |
| Unnamed | Ghidra `FUN_xxxxxxxx` / `DAT_xxxxxxxx`; docs use `func_xxxxxxxx` and occasionally `D_xxxxxxxx`/`unk_XX` (CLAUDE.md) | thousands | |

Where the Hungarian names come from: ghidra-mcp's naming gate. `set_global` **rejects** a global without a Hungarian prefix that matches its type unless `strict_mode=false` is passed, and `rename_function` warns when the first word is not in its verb list. The prefixes are therefore tool-driven, not a project decision.

Also inconsistent: unnamed functions are `FUN_` in Ghidra but `func_` in the docs (CLAUDE.md prescribes `func_`).

### Proposal

One convention for Ghidra, docs and the future C++ code, so a name can be searched in all three.

| Kind | Proposed | Example |
|---|---|---|
| Game functions | PascalCase, verb first (`Get/Set/Init/Load/Update/Draw/Request/Enter…`); scene handlers `Scene<Name>`; sub-state handlers `<Scene><What>` | `RequestScene`, `SceneStartup`, `StartupSplashUpdate` |
| SDK / libc | original names, never changed | `sceCdRead`, `memcpy` |
| Globals | `g_` + PascalCase, **no type prefix** | `g_MainContext`, `g_GameState`, `g_MissionNo`, `g_AltMissionAircraftMask` |
| Pointer globals | `g_p` only when the global is a pointer **to a single object that also has a name** (to tell them apart) | `g_MainContext` (object) vs `g_pMainContext` (pointer in `.sdata`) |
| File-local/static data tables | `k` + PascalCase for constant tables, `s_` for mutable statics | `kMissionIdTable` (0x00384800) |
| Struct fields | lower snake_case; unknown fields `unk_XX` (hex offset) | `scene`, `next_scene`, `cur_buf`, `unk_310` |
| Types (structs, enums) | PascalCase; enums values `kPascalCase` | `MainContext`, `CdFileEntry` |
| Unnamed | Ghidra defaults stay `FUN_`/`DAT_` inside Ghidra. Docs and code use `func_XXXXXXXX` / `D_XXXXXXXX` / `unk_XX` as CLAUDE.md says; tools convert | |
| Hypotheses | unchanged rule: no rename, plate comment `HYPOTHESIS: Name` | |

#### Pros
- Matches common modern C++ style; type information lives in the type, not the name, so names don't go stale when a type is refined (e.g. `u16` → enum).
- Struct field names in snake_case read naturally in translated code (`ctx->next_scene`) and match the field tables in the docs.
- Keeps the `g_` marker for globals, which is the useful part of the current style (globals are easy to spot in translated code).

#### Cons
- The ghidra-mcp gate fights it: every `set_global`/field rename needs `strict_mode=false`, and the plugin auto-prefixes struct fields on `create_struct`, so fields must be renamed after creation (one extra step per struct).
- Migration: 10 globals and ~50 struct fields must be renamed once (logged in `../rename-log.csv`).
- Loses the "type at a glance" hint in Ghidra listings (minor: Ghidra shows types anyway).

#### Alternative: adopt the tool's Hungarian style everywhere
- \+ No friction with ghidra-mcp; the 9 newest globals already follow it.
- − Hungarian is uncommon in modern C++; names change when types change; prefixes like `fl`, `dw`, `a` add noise in translated code; struct fields become `dwFlags` vs. docs `flags`.

**Recommendation:** the proposal above (no Hungarian), accepting the one-time `strict_mode=false` friction, because the names end up in the C++ code where they are read far more often than in Ghidra.
