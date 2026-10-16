# Pending PCSX2 checks

Checks that need the original game running in PCSX2 2.8.2, ordered by importance. Results go into a new `sessions-NN.md` file (latest: `sessions-10.md`); answered checks are removed from this list. Save states contain game memory: keep them in the save-state folder outside the repo (`*.p2s` is never committed).

General: open the debugger with **Debug → Open Debugger**. "Execute breakpoint" = Breakpoints tab → New → Execute, address as given. "Memory watch" = Memory view, go to the address, and read the value while the game runs or is paused.

## S5-A. Slice 5 block A batch: input-recording calibration, mission start (two runs, about 30 minutes)

- **Purpose:** (1) the frame offset between a PCSX2 power-on input recording and the port's game ticks (`../port/s5-plan.md` P2, `--p2m2-offset`); (2) reference RAM and pictures for block A: the mission set up in RAM at its first frame (heap exact) and the first gameplay frame as a GS dump.
- **Blocks:** validation of P2 and block A only. Documentation and translation go on meanwhile.
- **Preparation:** as for the S4 batch: save states uncompressed, Internal Resolution Native, PNG screenshots at "Internal Resolution (Aspect Uncorrected)" (F8), screen ratio 4:3, card S in slot 1, pad 2 **unplugged** (Settings → Controllers → Controller Port 2: Not Connected). Debug → Open Debugger. **Settings → Advanced → Enable Recording Tools** on.

### Run 1: calibration recording (cold boot, about 5 minutes)
1. Tools → Input Recording → New. File `s5_cal.p2m2` (in the Slices folder), record from **power-on / boot** (not from a save state).
2. Add Execute breakpoints **0x0022D748** (Start accepted on the title) and **0x0022D880** (confirm in the main menu).
3. Let the game boot to the title. Press **Start**. When 0x0022D748 fires: save `s5_cal_start.p2s`, write down the **recording frame number** PCSX2 shows (window title or the recording OSD), resume.
4. Press **X** (NEW GAME). When 0x0022D880 fires: save `s5_cal_confirm.p2s`, write down the frame number, resume. Press **X** (NORMAL).
5. When SS01 starts, stop the recording (Tools → Input Recording → Stop). Disable the breakpoints.
- **Record:** the file `s5_cal.p2m2`, both states, both frame numbers.

### Run 2: mission start for missions 1, 3 and 4 (about 25 minutes)
For each mission, one cold boot. Breakpoint for all three: Execute **0x0010A230** (first frame of the mission start, sub-state 0xA). Add it only once the loading screen is up (it fires once per mission).
1. **Mission 1:** Start, NEW GAME, NORMAL; skip SS01, the title card and the briefing with Start; in the campaign menu X on START MISSION, then X in the hangar on the default aircraft. While the **loading screen** shows: screenshot `s5_m1_loading.png`, then add the breakpoint. When it fires: save `s5_m1_start.p2s`, disable the breakpoint, resume. About 2 s after the first gameplay picture (or the mission's opening picture) appears: **GS dump** `s5_m1_frame` (the same dump function as the Oct-11 dumps) and screenshot `s5_m1_frame.png`.
2. **Mission 3:** title → LOAD → slot 1 (as run B of S4), CONTINUE, skip the title card and briefing with Start, START MISSION, X in the hangar on the default aircraft. Same steps: `s5_m3_loading.png`, `s5_m3_start.p2s`, GS dump `s5_m3_frame`, `s5_m3_frame.png`.
3. **Mission 4:** LOAD → **slot 4** (MISSION 04 / BLOCKADE), then as mission 3: `s5_m4_loading.png`, `s5_m4_start.p2s`, GS dump `s5_m4_frame`, `s5_m4_frame.png`.
- **Record:** every button with roughly when, which aircraft and colour were selected, anything shown before gameplay (e.g. an opening sequence), and whether the breakpoint fired once per mission.

## S2b-B. BGM player (optional, needs a card with the campaign cleared; about 5 minutes)

- **Purpose:** reference for the BGM player (OPTIONS shows it only after a cleared campaign), the one S2b page not yet checked (`../port/validation.md` S2b). Also gives the port a cleared save to test it with.
- **Blocks:** nothing.
- **Steps:** cold boot with the card in slot 1, Start, OPTIONS. Execute breakpoint **0x00244EA0**, open the BGM player. First hit: save `s2b_bgm_enter.p2s`, disable, resume. After about 2 s: screenshot `s2b_bgm.png`. Play one track and note the buttons.
- **Record:** state and screenshot names, the buttons, and a copy of the card folder (`Slices/`).

## 1. VU0 micro skinning reference data: part types 0 and 3

- **Purpose:** test vectors for the two VU0 skinning paths not yet captured. Path 0x00225110 (part type 1) was captured in run 05 (`sessions-05.md`) during the opening sequence before mission 1, where the other two paths never ran.
- **Blocks:** verification of the cutscene character engine only. Not the skeleton, not gameplay.
- **Background:** `FUN_002236A0` skins each character part by its type (+0x140): type 0 → `FUN_00224180` → loop 0x002250E8; type 1 → `FUN_00224410` → loop 0x00225110; type 3 → `FUN_00224848` → loop 0x00225140. Type 0 comes from the character data; type 3 is set only while a morph track is active (`FUN_00223F58`). The other in-engine sequences are the **no-save intro** (mission id 0x23, plays at boot when the card has no AC04 save) and the **sequence after mission 18** (id 0x24).
- **Preparation:** save states set to **Uncompressed**. For run A, a PCSX2 memory card **without** an AC04 save in slot 1 (a new empty card, or eject the card: Settings → Memory Cards → uncheck slot 1).
- **Steps:**
  1. In **Debug → Open Debugger**, add Execute breakpoints at **0x002250E8** and **0x00225140** only.
  2. Run A: boot the game with no AC04 save in slot 1. After the splash images the intro (in-engine, flight scene) starts by itself. Watch it to the end (it returns to the title).
  3. Run B (only if run A hits neither address, or hits only one): load a save state at the end of campaign mission 18 and let the post-mission sequence play, with the same breakpoints.
  4. When a breakpoint hits:
     1. Note the address and **a0, a1, a2, a3, ra**.
     2. **System → Save State** as `vu0_<address>_before.p2s`.
     3. **Step Out**, then save `vu0_<address>_after.p2s`.
     4. Disable that breakpoint and resume.
     Screenshots of the VU0 tabs are no longer needed: `tools/p2s_read.py STATE vu0` reads the registers from the state.
  5. If neither run hits an address, write that down: that path is then unused in the shipped data and only needs the code-level rewrite check.
- **Record:** which run, the address, a0–a3, ra and the state names.

Answered in run 05 (`sessions-05.md`): carrier takeoff after a rearm (subs 0x19–0x1B, 0x1D), VU0 skinning path 0x00225110.

Answered without PCSX2 (static analysis, see `../open-questions.md`): HUD warning texts, DATA 291–332 / BD 2 loaders, mission data for other aircraft, trial follow-up, result mode names.

## 2. Boot values at the first `SceneStep` (skeleton milestones C and E)

**Answered 2026-10-12** (owner: `scenestep_first.p2s` at the breakpoint, `scenestep_first_2.p2s` about 10 s later). `frame_count` = 0 at the breakpoint. Values: reverb address 0x001DB7C0 (was assumed 0x001FE0C0, fixed); voice states all 0 at the breakpoint and 4 for voices 42–47 at 10 s; TOC `04 02 F2 00 86 72 … 01 02 01 00 00 03 00 00 00 1D 7C 0F` (bytes 0x0C–0x17 were missing, fixed); GParam `01 00 02 00 01 00 1B 00` and radio timers as assumed. Full comparison: `../port/validation.md` E-1. The procedure below is kept for reruns.

- **Purpose:** check four values the port's SDK layer currently assumes (`../port/validation.md` C-2), and give milestone E its reference RAM image.
- **Blocks:** nothing in milestone C (the boot runs with the assumed values); milestone E's RAM comparison needs the save state.
- **Preparation:** **Settings → Advanced (or System) → Save state compression: Uncompressed**. Boot from a cold start (no fast boot, no save state loaded).
- **Steps:**
  1. In **Debug → Open Debugger**, add an Execute breakpoint at **0x00100B18** (`SceneStep`).
  2. Boot the game. On the **first** hit, **System → Save State** as `scenestep_first.p2s` (keep it in the save-state folder, outside the repo).
  3. Remove the breakpoint and continue until the title screen music plays (about 10 s), then pause and read in the Memory view:
     - 0x00410D08: 1 word (reverb work-area address from the SOUND reply; the port assumes 0x001FE0C0);
     - 0x00424FD0: 48 bytes (voice states).
  4. From the paused state at the breakpoint (or the save state), read:
     - 0x0029C980: 0x20 bytes (disc TOC; the port writes `04 02 F2 00 86 72`, byte 0x11 = 03);
     - 0x003FAAA0: 0x10 bytes (GS GParam: interlace, omode, ffmode, version);
     - 0x004BE0B0: 0x10 bytes (radio timers from the static constructor 0x002596F8).
- **Report:** the save state name, `frame_count` at the breakpoint (word at 0x0029F3A8 = MainContext 0x0029F380 + 0x28), and the values above.
