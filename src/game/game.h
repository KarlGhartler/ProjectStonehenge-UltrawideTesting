// Declarations of every translated game function (src/game/*.cpp). Names follow docs/symbols.csv;
// unconfirmed functions keep func_XXXXXXXX. Pointer parameters are 32-bit guest addresses (ADR-001).
#pragma once

#include "common/guest_call.h"
#include "common/guest_ref.h"
#include "common/types.h"
#include "game/main_context.h"

namespace ac04::game {

// ---- Boot path (boot.cpp) ----------------------------------------------------------------------
void Entry();
s32 Main();
void InitSubsystems();
void IopBoot();
void InitHardware();
void InitMainContext();
void InitGameStateGlobals();
void LoadBootData();
void InitRandomTable();

// ---- CD file system and read queues (cd_file.cpp) ----------------------------------------------
void InitCdFileSystem(u32 state, u32 toc_buffer, u32 /*unused*/);
u32 WaitDiscAndReadToc();
void BuildCdLsnTable();
u32 GetCdFileLsn(s32 file);
u32 PushCdReadRequest(s32 q, u32 lsn, u32 flags, u32 dest, u32 offset, u32 sectors);
u32 QueueCdRead(s32 q, u32 lsn, u64 flags, u32 dest, u32 offset, s32 count, s64 chunk);
u32 GetCdDoneTicket(s32 q);
void FlushCdQueue(s32 q);
s32 UpdateCdQueue();
void PollCdSync(u32 rec);
s32 IssueCdRead(u32 rec);
s32 ProcessCdRequest(u32 rec, s32 q);
u32 GetTblBuffer(s32 tbl);
u32 GetTblEntrySector(s32 tbl, s32 entry);
u32 GetTblEntrySize(s32 tbl, s32 entry);
u32 GetDataEntryAllocSize(s32 entry);
void LoadDataEntryAsync(s32 entry, u32 dest);
s32 IsDataLoadDone();

// ---- Display, DMA packet lists, SPR ring (display.cpp) -----------------------------------------
void ResetDoubleBuffer(u32 db);
u32 SwapDoubleBuffer(u32 db);
void ResetPacketList(u32 list);
void SendDmaPacketList(s32 channel, u32 list, s64 tte);
void InitGraphics();
void BuildDrawEnvPackets();
u32 WaitVSync();
void UpdateDisplayFrame(u32 even_field);
void DrawDisplayTransfer(u32 mode, u64 field, u32 brightness);
void PutDispAndDrawEnv();
u64 LerpRgba(u64 a, u64 b, u32 vec);
void InitSprRingBuffer();
u32 GetSprWorkBuffer();
void FlushSprWorkBuffer(u32 sadr, s32 qwc);

// ---- HDD module (hdd.cpp; dead on the retail path) ---------------------------------------------
u32 func_002597B8();
u32 func_002597C8();
void func_002598E8(u32 ctx, s32 op);
void func_00259910();
s32 InitHddModule(u32 ctx);
s32 func_0025ACD0(u32 ctx, u32 rec);
void func_0025B0B8(u32 ctx, s32 index, u32 a, u32 b, u32 c);
void func_0025BFC0(u32 worker);
s32 func_0025C058(u32 worker, s32 unit);
s32 func_0025C090(u32 worker, s32 unit);
void func_0025C198(u32 worker);
s32 func_0025C240(u32 worker, u32 req);
u32 func_0025C2C0(u32 src);
void func_0025E1D0(u32 job, s32 state);
s32 func_0025E1F0(u32 job);
void func_0025E218(u32 job);
s32 func_0025E248(u32 job, u32 request, u32 op);
void EnterHddErrorScene();

// ---- Work heap (heap.cpp) ----------------------------------------------------------------------
u32 ProbeLargestMallocSize();
void InitWorkHeap();
void FreeWorkHeap();
u32 WorkHeapAlloc(s32 size, u64 align);
u32 SetWorkHeapMark();
u32 ResetWorkHeapToMark();
u32 AllocAndLoadDataEntry(s32 entry);

// ---- Math helpers (math.cpp) -------------------------------------------------------------------
void func_00102D28(u32 m0, u32 m1, u32 angles);
f32 func_00104F10(u32 a, u32 b);

// ---- Movie state (movie_init.cpp) --------------------------------------------------------------
void InitMoviePlayback();
s32 func_00213C50(u32 block);

// ---- Options (options.cpp) ---------------------------------------------------------------------
void ApplyOptionSettings();
void func_001053C8(u32 block);
void InitOptionData();
void func_00105528(u32 dst);
u32 func_00105568(u32 dst);
void func_00105578(u32 dst);
void func_00105670(u32 p);
void func_00105690(u32 p);
void func_001056C8(u32 p);
void func_001056D8(u32 p);
void func_00105728(u32 p);
void func_00105748(u32 p);
void ApplyVibrationOptions(u32 options);
void ApplySoundOptions(u32 options);
void ApplyDisplayOffset(u32 options);
void ApplyBrightness(u32 options);
void func_0017BB58(u32 p);
void func_00246FE0();

// ---- Pads (pad.cpp) ----------------------------------------------------------------------------
u8 ReadPadData(s32 port, s32 slot, u32 buf);
void InitPadPort(s32 port, s32 slot, u32 rec);
void UpdatePadConnection(u32 rec);
u32 ScalePadAxis(u8 kind, u32 dz, u32 cal, u32 value);
void ConvertPadInput(u32 rec, u32 pad);
void UpdatePadButtonEdges(u32 pad, u32 out);
void BuildPadButtonMap(u32 map, u32 out);
void UpdateMappedPadButtons(u32 pad, u32 ext, u32 map_out);
s32 SendPadVibration(u32 pad, u32 rec);
s32 PadSystemInit();
void ShutdownPads();
s32 UpdatePads();
s32 StopPadVibration(u32 pad);
s32 SetPadVibrationEnabled(u32 pad, s64 enable, s64 test);
s32 StartPadVibration(f32 seconds, u32 pad);
s32 TickPadVibration(u32 pad);

// ---- Save system (save_init.cpp) ---------------------------------------------------------------
void func_0025E310();
u32 func_0025E330(u32 block, u32 port, u32 slot, u32 name);
u32 func_0025F298();
void InitSaveSystem(u32 save);
void func_0025F320(u32 save);

// ---- Scenes (scene.cpp) ------------------------------------------------------------------------
void RequestScene(u16 scene);
void RequestSubState(u16 sub);
void SetRequestFlags(u32 req);
void func_00100B08(u16 scene, u16 sub);
s32 SceneStep();
void SceneBootLoader();

// ---- SOUND client (sound_rpc.cpp) --------------------------------------------------------------
s32 BindSoundRpc(u32 client, u32 send, u32 receive, u32 param, u32 param2);
s32 SendSoundCommands();
s32 IsSoundRpcIdle(u32 client);
s32 func_002174D0(s32 type, u32 rec);
s32 QueueSoundVoiceCmd(u32 rec);
s32 func_00217680(u32 src, u32 dst, u32 size);
void func_00217758(u32 v);
u32 func_00217768();
void func_00217778(u32 v);
u32 func_002177C8();
u32 func_002177D0();
u32 func_002177E0(s32 n);
void func_002177F8(u32 v);
u32 func_00217808();
void func_00217818(u32 v);
u32 func_00217828();
void func_00217838(u32 v);
s32 func_00217858(s32 ticket);
s32 QueueSoundCmd8(u32 cmd);
s32 QueueSoundCmd12(u32 cmd);
s32 QueueSoundCmd16(u32 cmd);
void InitSoundVoiceTable();
void UpdateSoundVoiceStatus(u32 reply);
s32 AllocSoundVoice(u32 key_ptr);
s32 func_00217F28();
s32 func_00218008(u32 id, u32 out);
s32 func_002181E8(u32 rec);
void func_00218318(u32 rec);
s32 func_002183E8(u32 rec);
s32 func_00218620(u32 rec);
s32 func_002187B0(u32 rec);
u32 func_00218860();
s32 func_00218868(s16 bank_id, s16 sound, u32 out);
s32 func_00218A00(s32 n, u32 header, s64 spu);
void func_00218B00(s32 n, u32 header, s64 spu);
void func_00218C20();
u32 func_00218C80(s32 n);
u32 func_00218CB0(s32 n);
s32 func_00218CD8(s16 bank_id);
s32 func_00218D58();
u32 func_00218ED8();
void func_00218EE8(u32 v);
u32 func_00218EF0();
void func_00218EF8(u32 v);
s32 UpdateSoundRpc();
s32 InitSoundRpc();
void func_00219110();
void func_00219260();
void func_00219810(s32 n, u32 header);
u32 func_00219830();
u32 func_00219840();
f32 func_00219850(u32 to, u32 from, u32 angles);
void func_00219900(f32 angle, u32 left, u32 right);
void func_00219B48();
s32 LoadSoundBankData(s32 entry, u32 dest);
s32 StreamBdEntryToIop(s32 bank_id, s32 entry);
void func_0021A1C0();
void func_0021A328(f32 volume, s32 n);
void func_0021A680();
void func_0021A750(u32 src);
s16 func_0021A800(s64 law, s16 volume, s16 pan);
s32 func_0021AD48(s32 left, s16 right);
void func_0021ADF8();
void func_0021CCF0(f32 volume, s32 n);
u16 func_0021CD70(u32 pair, s16 offset);
void SetSoundOutputMode(u32 mode);
void func_0021CE38(u8 core, s16 left, s16 right);
void SetSoundDataInputVolume(u8 core, s16 left, s16 right);
void func_0021CF70(u8 core, u32 mode);
void func_0021CFA0(u8 core, s16 left, s16 right);
s32 func_0021D138(u32 rec);
s32 func_0021D680(s16 left, s16 right, u32 rec);
s32 StopSound(u32 rec);
s32 func_0021E240(u8 voice, s16 volume, u8 left_pan, u8 right_pan);

// ---- USB joystick client (usbjoy.cpp) ----------------------------------------------------------
s32 BindUsbJoyRpc();
u32 func_00109CA0(u32 v);
u32 func_00109CF8(u32 v);
u32 func_00109D50(u32 v);
u32 func_00109DA8(u32 v);
u32 func_00109E00(u32 v);
u32 func_00109E58(u32 v);
u32 func_00109EB0(u32 v);
u32 func_00109F08(u32 v);
s32 ReadUsbJoy(u32 out);

// ---- VAG stream client (vag_stream.cpp) --------------------------------------------------------
void func_0021E2D8();
s32 InitVagStream(u32 send, u32 receive, u32 voices);
s32 IsVagRpcIdle();
s32 QueueVagCmd(s32 slot, u32 type);
s32 SendVagCommands();
u32 func_0021EAB8(s32 slot, s32 pack, s32 entry);
void func_0021EC80(s32 slot);
void func_0021ECE8(s32 slot);
void func_0021ED60(s32 slot);
s32 func_0021EDB8(s32 slot, s32 pack, s32 entry, s32 param);
s32 func_0021EEB0(s32 slot, s32 pack_entry);
s32 func_0021EFD8(s32 slot, s32 left, s32 right);
void func_0021F138(s32 slot, s32 left, s32 right);
s32 func_0021F190(f32 time, s32 slot, s32 left, s32 right, s16 mode);
void func_0021F208(s32 slot, u32 left, u32 right);
void func_0021F298(s32 slot);
void func_0021F448();
u32 func_0021F8A8();
void func_0021F8B8();
void func_0021F908(s32 n, u32 lsn);
u32 func_0021F968(s32 n);
u32 func_0021F998(s32 pack, s32 entry);
u32 func_0021F9D8(s32 pack, s32 entry);
u32 func_0021FA08(s32 pack, s32 entry);
u32 func_0021FA38(s32 pack, s32 entry);
u32 func_0021FA68(s32 pack, s32 entry);
void func_0021FA98(s32 pack, s32 entry, u32 left, u32 right);
u32 func_0021FAD0(s32 n);
u32 func_0021FAE8(s32 n);
s32 func_0021FB00(s64 n);
void func_0021FB58();
void func_0021FC20();
s32 func_0021FD40(u32 slot, u16 id);
s32 func_0021FDF8(u8 mask);
void UpdateVagStream();

// ---- Static constructors (static_init.cpp) ----------------------------------------------------
void func_001E0A80(u32 state, s32 seed);
void func_001E0A20();
void func_001E94C0();
void func_001FF608();
void func_00210D88();
u32 func_002140A8(u32 alloc, u32 align);
void func_002124F8();
u32 func_00213C40(u32 feeder);
void func_00214030();
void func_002596F8();
void func_00100188();

}  // namespace ac04::game
