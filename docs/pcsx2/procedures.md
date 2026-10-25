# Pending PCSX2 checks

Checks that need the original game running in PCSX2 2.8.2, ordered by importance. Results go into a new run section of `sessions.md` (latest: run 13); answered checks are removed from this list. Save states contain game memory: keep them in the save-state folder outside the repo (`*.p2s` is never committed).

General: open the debugger with **Debug → Open Debugger**. "Execute breakpoint" = Breakpoints tab → New → Execute, address as given. "Memory watch" = Memory view, go to the address, and read the value while the game runs or is paused.

## R. References for the renderer and sound work of 2026-10-22 (one session, about 45 minutes)
- **Purpose:** compare the features built from code alone with the original: the sun flare's palette drawn on the GPU (`../port/validation.md` §2z), the depth copy of the cloud pass (§2ab ZC), the reverb (§2ab RV), the mission title card (§2z), and the memory card exchange with the port (`../formats/folder-memcard.md`).
- **Blocks:** nothing; the port runs without them. They decide whether the new code needs corrections.
- **Preparation:** Internal Resolution **Native**, PNG screenshots "Internal Resolution (Aspect Uncorrected)", 4:3, GS dumps **Uncompressed**, save states **Uncompressed**. Files into `Slices/Reference_1022/`.
- **Steps:**
  1. **Sun flare** (any campaign mission in daylight; mission 1 is fine): turn towards the sun until the flare rings show. Screenshot + GS dump (`r1_flare_full`). Then fly so that the sun is half behind a bomber or a cloud: screenshot + GS dump (`r1_flare_partial`). Save a state at each (`r1_flare_full.p2s`, `r1_flare_partial.p2s`).
  2. **Depth copy, no-save intro:** boot with no AC04 save in slot 1 (as section 1 run A). When the first cloud transition starts (about 22 s into the in-engine intro, right after the seagull shots), take a GS dump of 3 consecutive frames (`r2_intro_cloud_1` … `_3`) and screenshots. If possible repeat at the second transition (about 31 s).
  3. **Depth copy, mission 12:** any frame of free flight in mission 12 (campaign save or free mission): GS dump + screenshot (`r3_m12`).
  4. **Reverb:** with audio recording on (Tools → Start Video Capture, which records the sound too; or the audio dump option of the build), record (a) 15 s of the main menu moving the cursor up and down and opening/closing OPTIONS, (b) 30 s of flight in mission 1 or 4 including a missile launch and a hit. Files `r4_menu`, `r4_flight`.
  5. **Mission title card:** finish mission 1 (or load a state just before its end) and let the game reach the MISSION 02 title card ("IMMINENT THREAT"): screenshots at its start, while it zooms in, and when it stands (`r5_title_1` … `_3`).
  6. **Memory card round trip:** (a) copy the port's save folder `%LOCALAPPDATA%\ProjectStonehenge\saves\BASLUS-20152AC04` into a PCSX2 folder memory card, load it in PCSX2 and check the campaign progress and options; save once in PCSX2. (b) Copy the folder back into a copy of the port's `saves\` and load it in the port (give me the folder; I run the port side). Note anything that differs.
- **Record:** the state and dump names, what was on screen, and for step 6 what loaded correctly on each side.

## S7. Slice 7 batch: results screen and post-mission menu (one session, about 30 minutes; can follow C2)
- **Purpose:** reference data for scene 5 (results) and the post-mission menu (`../subsystems/results-startup.md` "Scene 5"), translated 2026-10-18 in `src/game/results_scene.cpp` and `src/game/post_mission_page.cpp`. The port cannot reach the results screen on its own yet: a `--mission` start diverges from PCSX2 (its RNG and heap differ), so replaying `b3_m4_full.p2m2` over it does not complete mission 4 (tried 2026-10-18). Hence test vectors and screenshots.
- **Blocks:** validation of S7 only.
- **Preparation:** as for S5-C. Files into `Slices/Slice7/`. Start from the end of C2 (`c2_results_f<frame>.p2s`), or play any campaign mission (mission 4 is fine) to MISSION ACCOMPLISHED and let the replay run into the results.
- **Steps:**
  1. Execute breakpoint **0x0024C328** (results sub 0). At the hit save `s7_r0_before.p2s`, Step Out, save `s7_r0_after.p2s`, remove the breakpoint.
  2. Execute breakpoint **0x00247BD8** (`ResultsUpdate`). Let it hit about 20 times (F5 between hits) so the opening has passed, then at three different moments while the tracks run on the map (units moving, kill markers appearing) save `tv_00247BD8_<n>_before.p2s`, Step Out, save `tv_00247BD8_<n>_after.p2s` (n = 1, 2, 3). Hold **R2** for the third one (5 steps per frame). Remove the breakpoint.
  3. Screenshots (F8 / Save Screenshot, internal resolution): the results page while the tracks run, after the tracks end (all kills counted), and the debriefing text page.
  4. Execute breakpoint **0x002476F0** (counts every kill at once). Press **X** on the results page: at the hit save `tv_002476F0_before.p2s`, Step Out, save `tv_002476F0_after.p2s`.
  5. Execute breakpoint **0x00246FF0** (result summary), then leave the results page (X until the screen fades out): at the hit save `tv_00246FF0_before.p2s`, Step Out, save `tv_00246FF0_after.p2s`.
  6. In the post-mission menu: screenshot with the cursor on each item (the help line changes). Execute breakpoint **0x00235F10**, choose the first item (continue): `tv_00235F10_before.p2s`, Step Out, `tv_00235F10_after.p2s`. Stop.
- **Record:** the mission played, the rank shown, which pages appeared after the results (story movie or not), anything unusual.

## S5-C. Slice 5 block C batch: sequences, replay, title demo, carrier, game over (one session, about 75 minutes)
- **Purpose:** reference data for block C (cutscenes and in-engine sequences, replay and title demo, carrier landing/takeoff, game-over and rearm menus; `../port/slices.md` §B.7): recordings that start from save states, with checkpoint states the port compares RAM against.
- **Blocks:** validation of block C only. Documentation and translation go on meanwhile. Section 1 below (VU0 skinning vectors) belongs to this batch too; do it in the same session if it is still open.
- **Preparation:** as for S5-B (states uncompressed, Internal Resolution Native, PNG screenshots, 4:3, pad 2 not connected, Enable Recording Tools on, GS dumps uncompressed). Files into `Slices/Slice5c/`. Recordings start exactly as in S5-B ("How a recording starts"): load the state paused, Tools → Input Recording → New, "Record from: Current frame / Save state", resume. Checkpoint states are named with the recording frame number shown by PCSX2.

### C1. Pre-mission sequence of mission 1 (`c1_m1_intro.p2m2`)
Start a new campaign (or load the card used for S5-A) and select mission 1. Add an Execute breakpoint at **0x0010BA38** (in-engine sequence sub-state 0x13). When it hits the first time, remove it and save `c1_start.p2s`; start the recording from there and **do not touch the pad** until the sequence ends and gameplay starts (or 3 minutes). Checkpoint states about every 20 s (`c1_f<frame>.p2s`) and one at the first gameplay frame (Execute breakpoint 0x0010A230, `c1_end_f<frame>.p2s`). Also two screenshots: the front view of the F-4E on the deck with the island behind it, and the low side view as it rolls (the port draws the aircraft almost black there: lighting check). Then stop.

### C2. Replay after mission 4 (`c2_m4_replay.p2m2`)
From the last state of B3 (or play mission 4 to MISSION ACCOMPLISHED). Add an Execute breakpoint at **0x0016E2D0** (`UpdateReplayPlayback`); at its first hit remove it, save `c2_start.p2s` and start the recording. Let the replay run without input (about 60 s), save `c2_f<frame>.p2s` every 15 s, then press START once and keep recording until the results screen; save `c2_results_f<frame>.p2s` there and stop.

### C3. Carrier landing and rearm (`c3_m3_carrier.p2m2`)
From the B4 state at the return line (`b4_f<frame>.p2s`, mission not yet complete), start the recording, follow the landing guidance and land. If the landing fails, the retry is fine: keep going. After touchdown press START to open the **rearm menu**, save `c3_rearm_f<frame>.p2s`, choose CHANGE WEAPON once (any entry), then TAKEOFF and fly the takeoff run until normal control returns (sub 0xB); save `c3_takeoff_f<frame>.p2s` and stop. Checkpoint states about every 30 s in between.

### C4. Game over (`c4_m4_gameover.p2m2`)
From `s5_m4_start.p2s`: start the recording and fly into the ground. When the game-over menu appears (RETRY / QUIT MISSION / RETURN TO TITLE) save `c4_menu_f<frame>.p2s`, choose RETRY, and keep recording until the mission restarts (Execute breakpoint 0x0010A230 hits: `c4_retry_f<frame>.p2s`). Stop.

### C5. Title demo (`c5_demo.p2m2`)
Boot normally to the title screen and leave the pad untouched. Add an Execute breakpoint at **0x0022CC68** (`StartTitleDemo`); when it hits, remove it, save `c5_start.p2s` and start the recording without input. Save `c5_f<frame>.p2s` every 15 s until the demo returns to the title (`c5_end_f<frame>.p2s`). Stop.

### C6. No-save intro (optional, if section 1 run A is done: the same boot)
With no AC04 save in slot 1, boot; at the first hit of **0x0010BA38** save `c6_start.p2s`, record without input to the end of the intro with states every 20 s (`c6_f<frame>.p2s`).

### C7. Mid-mission sequence (optional, only if a card reaches one of the missions with mission ids 4, 5, 10, 14 or 15, e.g. campaign mission 12)
Execute breakpoint **0x0010B500** (sub-states 0xD–0xF); at the first hit save `c7_start.p2s` and record without input until gameplay resumes (states every 15 s).

- **Record:** per recording the file name, the checkpoint state names, which path was taken (C3: landing retries; C4: crash place), anything unusual.

## S5-B. Slice 5 block B batch: gameplay recordings, test vectors, effect frames, HUD, sound (one session, about 90 minutes)
- **Purpose:** reference data for block B (flight, AI, mission rules, weapons, effects, HUD, mission sound; `../port/slices.md` §B.6): input recordings the port replays with full-RAM checkpoints, entry/exit states of the riskiest functions (test vectors), GS dumps of effect-heavy frames, HUD pictures and a sound capture.
- **Blocks:** validation of block B only. The port work (documentation, then translation) goes on meanwhile.
- **Preparation:** as for S5-A (save states uncompressed, Internal Resolution Native, PNG screenshots "Internal Resolution (Aspect Uncorrected)", 4:3, pad 2 not connected, Enable Recording Tools on) and **GS dump compression: Uncompressed** (Settings → Graphics → Debug/Advanced; the S5-A dumps came as `.zst` twice). Files into `Slices/Slice5b/`. Keep `s5_m3_start.p2s` and `s5_m4_start.p2s` from run 12: every recording starts from them.
- **How a recording starts (all of B1-B4):** load the state (`System → Load State` from file), leave it **paused** (it is at the breakpoint 0x0010A230; remove the breakpoint), then Tools → Input Recording → **New**, "Record from: **Current frame / Save state**", file name as given. Then resume and play. The recording's frame 0 is the anchor. Stop with Tools → Input Recording → Stop.
- **Checkpoint states during a recording:** pause (the pause hotkey, or System → Pause), `System → Save State` to file with the given name **including the recording frame number** shown by PCSX2 (e.g. `b1_f0600.p2s`), resume. Exact frames do not matter; the number in the name does.

### B1. Mission 4 free flight, 60 s (`b1_m4_flight.p2m2`)
From `s5_m4_start.p2s`. Fly without firing: about 10 s level, then climbs, dives, rolls both ways, full throttle and air brake (R1/R2 or as mapped), a few seconds very low over the terrain if possible (no crash). Checkpoint states about every 10 s: `b1_f<frame>.p2s` (6 states).

### B2. Mission 4 firing run, about 90 s (`b2_m4_fire.p2m2`)
From `s5_m4_start.p2s`. Fire the gun in bursts, lock and fire missiles at aircraft or transports (at least 4, some hits), drop the special weapon (UGBM) on a ground target or the ground, change targets (the target button). Checkpoint states about every 15 s (`b2_f<frame>.p2s`) and one right after the first missile hit (`b2_hit_f<frame>.p2s`).

### B3. Mission 4 to the end (`b3_m4_full.p2m2`)
From `s5_m4_start.p2s`. Play mission 4 normally until MISSION ACCOMPLISHED (or failure) and the replay/results start; stop the recording when the results screen appears. Checkpoint states at every radio message about mission progress (new objective, phase change) and about every 60 s otherwise: `b3_f<frame>.p2s`. Note what happened around each (e.g. "transports appeared").

### B4. Mission 3 to the return line (`b4_m3.p2m2`)
From `s5_m3_start.p2s`. Destroy the targets and fly to the return line until the carrier landing sequence starts (AWACS "Approaching return line..."; sub-state 0x14, block C). Checkpoint states about every 60 s and at the return line (`b4_f<frame>.p2s`).

### B5. Function test vectors (entry/exit pairs)
During any free play in mission 4 (load `s5_m4_start.p2s`, no recording needed). For each address below, one at a time: add the Execute breakpoint; when it hits, note **a0, a1, a2, a3, ra**, save `tv_<address>_before.p2s`; then **Step Out** (Debug → Step Out) and save `tv_<address>_after.p2s`; disable the breakpoint and go on. If Step Out stops at another breakpoint, disable the others first.

| Address | Function | When it hits |
|---|---|---|
| 0x00129500 | `UpdateFlightDynamics` (player flight model step) | every frame in flight: take one while turning |
| 0x00122638 | player collision (terrain, units) | every frame: take one flying low over terrain |
| 0x0017C020 | point-to-triangle distance (collision core, VU0 flag delay) | when flying low; take the first hit |
| 0x0012B540 | `UpdateAITarget` | every frame per unit: take one while enemies are in the air |
| 0x0011D580 | group target selection | when AI groups pick targets (soon after the start) |
| 0x0014C8F0 | `FireBullet` | press the gun button |
| 0x0014D8B0 | `FireMissile` | fire a missile |
| 0x0014FA88 | `FireSpecialWeapon` | drop the UGBM |
| 0x001DD9D8 | rocket/missile trail spawn (random reads) | right after a missile launch |
| 0x002528C8 | `QueueRadioChatter` (probability table, `rand`) | when a radio chatter line is about to play (an enemy shot down, a missile near) |

### B6. Effect-heavy frames (GS dumps) and HUD pictures
In B2 or a free run of mission 4:
1. **GS dump** `b6_explosion` with a large explosion and smoke on screen (a transport or aircraft destroyed nearby), and **GS dump** `b6_missiles` with several missile trails and afterburner heat haze (behind another aircraft). Screenshot of the same frames (`b6_explosion.png`, `b6_missiles.png`).
2. **Screenshots** (F8): `hud_lockon.png` (lock-on box on a target), `hud_warning.png` (MISSILE ALERT / warning), `hud_map.png` (the full map view, the map button), `hud_message.png` (a centre message such as TARGET DESTROYED), `hud_cockpit.png` (cockpit view).
3. **16:9:** Options → screen settings → 16:9, then `hud_169.png` in flight; set it back to 4:3.

### B7. Sound capture (about 20 s)
Tools → Start Video/Audio capture (as for the S3 movie capture), during mission 4 flight with the engine at varying throttle, a gun burst, a missile launch and at least one radio voice line. File `b7_sound` (whatever container PCSX2 writes; `tools/research/flac_mp4.py` reads the MP4/FLAC one).

- **Record:** for each recording its file and the checkpoint state names; for B5 the a0-a3/ra values per address; anything unusual (the game paused, a load, a sequence).      

## 1. VU0 micro skinning reference data: part types 0 and 3

- **Purpose:** test vectors for the two VU0 skinning paths not yet captured. Path 0x00225110 (part type 1) was captured in run 05 (`sessions.md` run 05) during the opening sequence before mission 1, where the other two paths never ran.
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

Answered in runs 11 and 12 (`sessions.md` run 11, `sessions.md` run 12, 2026-10-17): the S5-A batch (calibration recording, mission start of missions 1, 3 and 4). Answered in run 05 (`sessions.md` run 05): carrier takeoff after a rearm (subs 0x19–0x1B, 0x1D), VU0 skinning path 0x00225110.

Answered without PCSX2 (static analysis, see `../open-questions.md`): HUD warning texts, DATA 291–332 / BD 2 loaders, mission data for other aircraft, trial follow-up, result mode names.

## 2. Boot values at the first `SceneStep` (skeleton milestones C and E)

**Answered 2026-10-12** (User: `scenestep_first.p2s` at the breakpoint, `scenestep_first_2.p2s` about 10 s later). `frame_count` = 0 at the breakpoint. Values: reverb address 0x001DB7C0 (was assumed 0x001FE0C0, fixed); voice states all 0 at the breakpoint and 4 for voices 42–47 at 10 s; TOC `04 02 F2 00 86 72 … 01 02 01 00 00 03 00 00 00 1D 7C 0F` (bytes 0x0C–0x17 were missing, fixed); GParam `01 00 02 00 01 00 1B 00` and radio timers as assumed. Full comparison: `../port/validation.md` E-1. The procedure below is kept for reruns.

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

## Functions waiting for a PCSX2 answer (formerly `pcsx2/procedures.md`)

User rule (2026-10-16, `../port/slices.md` §B): a function that cannot be translated with confidence from the code alone is not guessed. Its translation is skipped (or done with the open point marked `// HYPOTHESIS:`) and logged here. The User collects the answers in the next PCSX2 batch (this file).

| Function | Question | What to capture | Status |
|---|---|---|---|
| `func_00116060` 0x00116060 (star corner projection, `../subsystems/sky.md`) | The MAC-flag read at 0x0011609C (`cfc2 v0, $17`, `andi 0x11`) follows `vmulq.xyz vf9`; PCSX2 has no GameDB patch for it. Does it ever return −1 there (stars behind the camera), i.e. does PCSX2 clear the w flags of a `.xyz` op? | Breakpoint at 0x001160A4 during a night mission with stars visible (mission with R+0xC0 bit 1): record `v1` (= flags & 0x11) over a few hits, plus a GS dump of the frame. Expected (HYPOTHESIS): always 0. | open |
| `func_001152D8` 0x001152D8 (cloud sprites, `../subsystems/sky.md`) | vf2 is loaded with `qmtc2` from a GPR whose low 64 bits come from `ld W+0xB0`; the upper 64 bits (vf2.z, vf2.w) are whatever the GPR held before. The translation assumes 0 (HYPOTHESIS), which makes the sprite depth that of the projected centre. | Breakpoint at 0x001155EC in a mission with clouds (R+0xC0 bit 2): record the full 128-bit `v0` and, after the `qmtc2`, VU0 `vf2` over a few hits. Expected: z = w = 0. | open |
| `func_00113D70` 0x00113D70 (animated cloud texture) | The corner dwords take their z / w halves from VU0 vf15, which the function never writes (left by the last `func_00114ED8` call or other VU0 code). The port carries vf15 in its host VU0 state; only the RAM checkpoint is affected (GS: ZTST always, Z masked). | Breakpoint at the function entry in a cloud mission: record VU0 `vf15`; compare with the port's value at the same frame. Only needed if the RAM checkpoint shows a difference in the cloud packet. | open |
