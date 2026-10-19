# PCSX2 debugging sessions: run 12 (slice S5 block A batch, run 2 missions 3 and 4)

- Emulator: PCSX2 2.8.2, original disc image (NTSC-U, SLUS_201.52), settings as in `sessions-10.md`.
- Run by the owner on 2026-10-17, following `procedures.md` item S5-A run 2 (missions 3 and 4). With this the S5-A batch is complete and removed from `procedures.md`.
- All files stay outside the repo (folder `Slices/Slice5/`; paths in `CLAUDE.local.md`). Only derived values are recorded here.

## Mission 3

Files: `s5_m3_loading.png`, `s5_m3_loading_2.png`, `s5_m3_start.p2s` (0x0010A230), GS dump `s5_m3_frame.zst`, `s5_m3_frame.png`.

- Sequence: LOAD file 1, CONTINUE, title card and briefing skipped, START MISSION, default aircraft (state: aircraft 2 = F-4E, colour 0, weapon 0; mission id 3, mode 1, difficulty 3).
- `s5_m3_loading.png`: the loading screen "03 / "THE NORTHERN EYE" / MISSION OBJECTIVES: DESTROY ALL INDICATED TARGETS AND RETURN TO BASE. / NOW LOADING...".
- `s5_m3_loading_2.png`: a fully black frame. The owner saw a black screen between the loading screen and gameplay. Explained in the port (`../port/validation.md` S5-A3): after Start the loading screen fades to black, the breakpoint 0x0010A230 runs on the black frame, then the mission fades in; the screenshot was taken while paused there.
- `s5_m3_frame.png`: snowy mountains, HUD with TIME 15'00"00, AWACS "This combat area is within enemy airspace. Keep an eye out for bandits.", SPEED 261, ALT 4300.

## Mission 4

Files: `s5_m4_loading.png`, `s5_m4_loading_2.png` (black), `s5_m4_start.p2s`, GS dump `s5_m4_frame.zst`, `s5_m4_frame.png`.

- Sequence: LOAD file 4 (MISSION 04 / BLOCKADE), then as mission 3 (state: mission id 0x10, aircraft 2, colour 0, weapon 0, mode 1, difficulty 3).
- `s5_m4_frame.png`: high altitude over a hazy mountain range, HUD TIME 12'00"00, SPEED 260, ALT 13333, AWACS "Mobius 1, SkyEye here. You should be getting visual on the transports soon.", a wingman to the right.

## Notes

- The GS dumps came zstd-compressed again (PCSX2 setting "GS dump compression"); unpacked for research as before.
- Effect clock: the word at effect block +0x6249C read by `InitEffects` before it is written in the scene held 0x80000000 (mission 3), 0x80312B1F (mission 1, run 11) and 0 (mission 4) in these states: memory left from the briefing (`../port/validation.md` S5-A3).
- New card from the owner: `Memory Card Folder/Mcd001_converted.ps2` with a fully cleared campaign (already used for S2b-B in run 11). It gives access to the BGM player, the interlude viewer and all missions in free mission.
