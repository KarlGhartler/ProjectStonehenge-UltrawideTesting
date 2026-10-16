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
u8 GetHddState(u32 ctx);
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
void func_002597E8(u32 error);
u16 func_00259878();
u16 func_00259898();
u32 func_00259C90(u32 ctx);
u8 GetHddSaveMode(u32 ctx);
s32 func_0025AEE0(u32 ctx);
s32 func_0025B0D8(u32 ctx);
s32 func_0025B6C8(u32 ctx);
s32 func_0025B948(u32 ctx, s32 index);
s32 func_0025BBD0(u32 ctx, s32 index);
s32 func_0025BED8(u32 ctx);
void EnterHddErrorScene();

// ---- Work heap (heap.cpp) ----------------------------------------------------------------------
u32 ProbeLargestMallocSize();
void InitWorkHeap();
void FreeWorkHeap();
u32 WorkHeapAlloc(s32 size, u64 align);
u32 GetWorkHeapFree();
u32 RewindWorkHeapTo(u32 addr);
u32 SetWorkHeapMark();
u32 ResetWorkHeapToMark();
u32 AllocAndLoadDataEntry(s32 entry);
u32 PushWorkHeapMark();
u32 PopWorkHeapMark();

// ---- Random table (random.cpp) --------------------------------------------------------------------
void SetRandomIndex(u32 index);
f32 NextRandomFloat();

// ---- Effect pools (effect_pools.cpp) --------------------------------------------------------------
void AllocEffectBlock();
void InitEffects(u32 block30);
void func_00155AE0();
void func_001568F0(u32 pool);
void func_001D4200(u32 rec);

// ---- Scene 4: front end (menu.cpp) ---------------------------------------------------------------
void EnterMenu(u16 page);
void SetMenuSubPage(u16 sub);
void MenuGoTo(s64 kind, u16 arg);
void SetMenuDisplayMode(MainContext* ctx, u16 w, u16 h);
void MenuEnterPage();
void MenuEnterSubPage();
void SceneMenu();
s16 GetMissionId(u32 kind, s32 n);
void ValidateCampaignSelection();
void ApplyCampaignSelection();
void ResetMenuDataLoad(u32 q);
s32 QueueMenuDataLoad(u32 q, u16 entry, s32 count, u32 table);
s32 UpdateMenuDataLoad(u32 q);
void InitFade(u32 fade, s64 clear);
void StartFade(f32 duration, u32 fade, s64 out);
void UpdateFade(u32 fade);
void ResetMenuBgm();
void PlayMenuBgm(s16 track);
void FadeOutMenuBgm(f32 time);
void UpdateMenuBgm();
void StartTitleDemo();
void func_0022CC50(s64 mode);
void UpdateMainMenuHelp(u32 obj);
void MainMenuExitWithFade(u32 obj, u32 page);
void MainMenuExit(u32 obj, u32 sub);
void MainMenuBack(u32 obj, u32 tree);
void MainMenuSelect(u32 obj, u32 tree);
void MainMenuSetup(u32 obj, s32 mode);
u32 CreateMainMenu();
void UpdateTitleTimers(u32 obj);
void MainMenuUpdate(u32 obj);
f32 EvalCurve(f32 t, u32 curve);
void DrawTitleLogo(u32 list, s32 index, u32 obj);
void DrawMainMenu(u32 obj);
void DrawTitleBackground(u32 list, u32 obj);
void EnterTitle();
void LoadTitle();
void RunTitle();
s32 GetSaveSizeKb();

// ---- Front-end widgets (menu_widgets.cpp) -------------------------------------------------------
void DrawBlackFade(f32 level, u32 list, s32 index);
f32 GetMenuPulse();
void ClearMenuTabLevels(u32 levels);
f32 ApproachValue(f32 value, f32 target, f32 rate);
void UpdateMenuTabLevels(u32 levels, s16 master, u32 tabs);
void ResetMenuFade(u32 fade);
void UpdateMenuOverlay(u32 fade);
u32 BuildMenuDrawEnv(u32 p, s32 w, s32 h);
u32 BuildFrameDrawEnv(u32 p);
u32 BuildScanlineBands(u32 p, u32 frame, s32 w, s32 h);
u32 BuildZoomBlur(f32 t, f32 scale, u32 p, s32 w, s32 h);
void DrawMenuFrameFilter(f32 level, u32 list, s32 index, u64 mode);
void DrawMenuAlphaFilter(f32 level, u32 list, s32 index);
u32 BuildCenteredSprite(u32 p, s32 x, s32 y, u64 color, u32 desc);
u32 BuildCenteredTexSprite(u32 p, s32 x, s32 y, u64 color, u32 desc, u64 tex0);
u32 BuildMenuTabs(u32 p, u32 levels);
void DrawPageFadeBar(f32 level, u32 list, s32 index);
void DrawMenuTabs(u32 list, s32 index, u32 levels);
void DrawMenuFade(u32 list, s32 index, u32 fade);
void DrawMenuFadeFrame(u32 list, u32 fade);
void ResetConnectorAlphas(u32 conns, s32 count);
void UpdateConnectorAlphas(u32 conns, s32 count, s32 active);
void ResetListWidgets(u32 w, s32 count);
void SetListWidgetState(u32 w, s32 cmd);
void UpdateListWidget(u32 w, s64 active, u32 pad);
void UpdateListWidgets(u32 w, s32 count, s32 active, u32 pad);
u32 BuildMenuString(f32 count, f32 scale, u32 p, s32 x, s32 y, u32 str);
u32 BuildFrameOutline(f32 t, u32 p, u32 rect, u32 color);
void DrawFrameOutlines(u32 list, s32 index, s32 count, u32 rects, u32 times, u64 mask);
void DrawAllFrameOutlines(u32 list, s32 index, s32 count, u32 rects, u32 times);
void InitCrosshair(u32 c);
void MoveCrosshair(u32 c, s32 steps, s32 x, s32 y);
void MoveCrosshairAuto(u32 c, s32 x, s32 y);
void UpdateCrosshair(u32 c);
void DrawCrosshair(u32 list, s32 index, u32 c);
void BeginMenuTextLayer(u32 list, s32 index);
void EndMenuTextLayer();
void DrawMenuText(f32 count, s32 x, s32 y, s64 style, u32 str);
void DrawMenuTextColored(f32 count, s32 x, s32 y, s64 style, u32 str, u32 color);
u32 GetStyleGlyph(u64 style, s32 ch);
void DrawPulseBox(u32 list, s32 index, u32 color, s32 x, s32 y, s32 w, s32 frame);
u32 BuildGauge(f32 t, u32 p, s32 x, s32 y, s32 w);
u32 BuildFilledBox(u32 p, u64 color, u64 xy0, u64 xy1);
u32 BuildIconBox(u32 p, u64 x, u64 y, s32 w, s32 h, s32 highlight);
void ResetPanel(u32 panel);
void InitPanel(u32 panel, u64 count, u32 rects, u32 crosshair);
void UpdatePanel(u32 panel);
void SetPanelState(u32 panel, s64 cmd);
void DrawPanel(u32 list, s32 index, u32 panel);
s32 GetProgressBgmTrack(s32 kind, s32 mission);
u32 BuildStyledString(f32 count, u32 p, s32 x, s32 y, u32 str, u64 style, u32 color, u32 color2);
void DrawListWidgets(u32 list, s32 index, u32 w, s32 count);
void LayoutConnector(s32 count, s32 y_parent, s32 y_child, u32 scales, u32 ys, u32 levels);
u64 GetConnectorColor(f32 s, f32 a);
u32 BuildConnectorCap(f32 s, f32 pulse, u32 p, s64 x, s64 y, s64 w, s64 h);
u32 BuildConnector(f32 alpha, u32 p, u32 conn);
void DrawConnectors(u32 list, s32 index, u32 conns, s32 count);
void ResetMenuTree(u32 tree);
void InitMenuTree(u32 tree, u32 count, u32 cols, u32 conns, u32 masks, u32 parents);
void UpdateMenuTree(u32 tree, u32 pad);
s16 GetMenuTreeSelection(u32 tree);
void DrawMenuTree(u32 list, s32 index, u32 tree);
void StoreMenuTreeCursors(u32 cols, s32 count, u32 tree);
void UploadMessageBoxGlyphs(u32 list, s32 index, u32 mb);
void InitMessageBox(u32 mb);
void MeasureMessageBox(u32 mb);
void SetMessageBoxText(u32 mb, s32 id);
void UpdateMessageBox(f32 speed, u32 mb, s32 frame);
void DrawMessageBox(u32 list, s32 index, u32 mb);

// ---- Message window and text engine (message_window.cpp) ----------------------------------------
void InitMessageWindow(u32 win, s32 kind);
void LoadMessageWindow(u32 win, u32 font, u32 text, u32 gfx);
void ShowMessagePage(u32 win, s16 page);
void HideMessageWindow(u32 win);
void UpdateMessageWindow(u32 win);
void DrawMessagePage(u32 win, u32 list, s32 index, u32 page);
void DrawMessageWindow(u32 win, u32 cam, u32 list);
u32 GetMessage(u32 id);
s32 IndexMessages(u32 block);
s32 GetMessageCount();
s32 LoadFontBlock(u32 block);
u8 GetGlyphWidth(u32 code);
u16 GetMessageCharWidth(u32 id, s64 index);
void SelectFont(u32 font, u32 glyphs);
s32 GetGlyphStringLength(u32 s);
s32 GetMessageLength(u32 id);
u16 GetMessageChar(u32 id, s64 index);
void UploadMessageGlyphs(u32 list, s32 index, s64 id, u64 dbp);

// ---- Campaign progress and unlocks (campaign.cpp) ----------------------------------------------
s16 GetAircraftField8(s32 aircraft);
s16 GetAircraftAtListPosition(u64 n);
void AdvanceCampaignMission();
void RaiseOwnership(s32 aircraft, u64 word, s64 level);
bool CheckOtherAircraftBuyable(u64 word, s32 aircraft);
bool CheckAllMissionsRankedS(u64 difficulty);
void UnlockMissionColor2(s32 mission);
void ApplyRankUnlocks();
void UnlockAircraftUpToMission(s64 mission);
void ApplyClearUnlocks();
void MarkCampaignCleared();

// ---- Save / load page (save_page.cpp) ----------------------------------------------------------
s32 ComputeSaveChecksum(u32 image);
bool CheckSaveChecksum(u32 image);
void InitSaveFileImage(u32 image);
void SetSavePageStatus(u32 obj, s32 status);
s16 GetSaveHelpMessage(u32 obj);
s16 GetLoadHelpMessage(u32 obj);
void UpdateSavePageHelp(u32 obj);
void SelectSaveSlot(u32 obj, u64 slot);
void UpdateSaveSlotSummaries(u32 obj);
u32 CreateSavePage();
void LeaveSavePage(u32 obj);
void PrepareSaveImage(u32 obj);
void ApplyLoadedSave(u32 obj);
void SavePageUpdate(u32 obj);
void DrawSaveSlotTexts(u32 list, s32 index, u32 obj);
void DrawSaveSlotFrames(u32 list, s32 index, u32 obj);
void DrawSavePage(u32 obj);
void EnterSavePage();
void LoadSavePage();
void RunSavePage();

// ---- Options page (options_page.cpp) -----------------------------------------------------------
void UpdateOptionsHelp(u32 obj);
void SyncOptionToggles(u32 cols, u32 flags);
void SetOptionsView(u32 obj, s32 view);
u32 CreateOptionsPage();
void OptionsBack(u32 obj, u32 tree);
void OptionsSelect(u32 obj, u32 tree);
void OptionsListUpdate(u32 obj);
void OptionsAdjustCancel(u32 obj);
void OptionsAdjustAccept(u32 obj);
void ScreenPositionUpdate(u32 obj);
void BrightnessUpdate(u32 obj);
void OptionsUpdate(u32 obj);
void DrawScreenFrame(u32 list, s32 index, u32 obj);
void DrawBrightnessTexts(u32 list, s32 index, u32 obj);
u32 BuildGreyRamp(u32 p);
u32 BuildLevelSquares(u32 p);
u32 BuildColorRamps(u32 p);
void DrawBrightnessPatterns(f32 level, u32 list, s32 index, u32 obj);
void DrawOptionsPage(u32 obj);
void EnterOptionsPage();
void LoadOptionsPage();
void RunOptionsPage();

// ---- Records page (records_page.cpp) -----------------------------------------------------------
void CombSortDescending(u32 a, s32 n);
void BuildUsageList(u32 obj, u32 own, u32 totals);
void SelectRecordsPage(u32 obj, u64 page);
void RecordsPageInput(u32 obj);
void AnimateRecordBars(u32 obj);
u32 CreateRecordsPage();
void RecordsUpdate(u32 obj);
void DrawRecordsTexts(u32 list, s32 index, u32 obj);
void DrawRecordIcons(u32 list, s32 index, u32 obj, s64 mask);
void DrawRecordsPage(u32 obj);
void EnterRecordsPage();
void LoadRecordsPage();
void RunRecordsPage();

// ---- Controller page (controller_page.cpp) -----------------------------------------------------
void UpdateControllerHelp(u32 obj);
void SetControllerCursor(u32 obj, s32 row, s32 col);
void StoreControllerSettings(u32 obj, u32 words);
void ControllerInput(u32 obj);
void AnimateControllerRows(u32 obj);
u32 CreateControllerPage();
void ControllerUpdate(u32 obj);
void DrawControllerTexts(u32 list, s32 index, u32 obj);
void DrawControllerImage(u32 list, s32 index, u32 obj);
void DrawControllerGrid(u32 list, s32 index, u32 obj);
void DrawControllerPage(u32 obj);
void EnterControllerPage();
void LoadControllerPage();
void RunControllerPage();

// ---- Sound settings page and BGM player (sound_page.cpp) ---------------------------------------
void UpdateSoundHelp(u32 obj);
u32 CreateSoundPage();
void SoundUpdate(u32 obj);
void ApplySoundPageValues(u32 obj);
void DrawSoundTexts(u32 list, s32 index, u32 obj);
void DrawSoundMarks(u32 list, s32 index, u32 obj);
void DrawSoundPage(u32 obj);
void EnterSoundPage();
void LoadSoundPage();
void RunSoundPage();
void UpdateBgmPlayerHelp(u32 obj);
void SetBgmPlayerCursor(u32 obj);
u32 CreateBgmPlayer();
void UpdateBgmPlayerTimer(u32 obj);
void BgmPlayerInput(u32 obj);
void BgmPlayerUpdate(u32 obj);
void DrawBgmPlayerTexts(u32 list, s32 index, u32 obj);
void DrawBgmPlayer(u32 obj);
void EnterBgmPlayer();
void LoadBgmPlayer();
void RunBgmPlayer();

// ---- Mission results (results.cpp) -------------------------------------------------------------
void func_0017BE38();
void UpdateMissionRecord(u32 rec);
void ApplyMissionResults();
void func_0024CBE0();

// ---- Math helpers (math.cpp) -------------------------------------------------------------------
void func_00102D28(u32 m0, u32 m1, u32 angles);
void func_00102EF0(u32 out, u32 a, u32 b);
void func_00103070(u32 out, u32 base, u32 euler, u32 offset);
void func_001030F0(u32 out, u32 euler, u32 v);
void func_00103290(u32 out, u32 m);
void func_00103818(u32 out, u32 p, u32 q, u32 r);
f32 func_00103948(f32 yaw, u32 a, u32 b);
f32 func_001039D8(f32 yaw, u32 a, u32 b);
f32 func_00104FA8(u32 v);
void func_00109658(u32 out, u32 a, u32 b, u32 c);
f32 func_00104F10(u32 a, u32 b);

// ---- Movie state (movie_init.cpp) --------------------------------------------------------------
void InitMoviePlayback();
s32 func_00213C50(u32 block);

// ---- Movie player (movie.cpp) ------------------------------------------------------------------
void StartMovie(u32 desc);
void ResetMovieTimer();
s32 DecodeMovieFrame(u32 desc, s64 stop);
void UpdateMovieAudioFade();
void ClearMovieRequest(u32 desc);
void RequestMovieStop(f32 time);
s32 InitSmvReader(u32 r, u32 arena, u32 lsn, u32 desc);
s32 ReadSmvHeader(u32 r, s64 stop);
s32 ReadSmvFirstChunk(u32 r, s64 stop);
s32 StartSmvReader(u32 r, s64 stop);
s32 StreamSmvReader(u32 r, s64 stop);
s32 DrainSmvReader(u32 r, s64 stop);
s32 FadeOutSmvReader(u32 r);
s32 FinishSmvReader(u32 r);
s32 StepSmvReader(u32 r, s64 stop);
s32 DrawMoviePicture(u32 r, u64 dbp);
u32 AddPacketTest2(u32 p);
u32 AddPacketAlpha2(u32 p);
u32 AddPacketTex1_2(u32 p);
u32 AddPacketBitBltBuf(u32 p);
u32 AddPacketTrxPos(u32 p);
u32 AddPacketTrxReg(u32 p);
u32 AddPacketTrxDir(u32 p);
u32 AddPacketTexFlush(u32 p);
u32 AddPacketTex0_1(u32 p);
u32 AddPacketPrim(u32 p);
u32 AddPacketSprites(u32 p, u64 prim, u64 count);
void SendMoviePacket(u32 p, u32 chan);
s32 StopMovieAudioIfRunning(u32 a);
s32 IsMovieAudioIdle(u32 a);
s32 SetMovieAudioBuffer(u32 a, s64 size);
s32 SendMovieAudioBlock(u32 a, u32 src, s64 half);
s32 IsMovieAudioDmaBusy(u32 a);
s32 StartMovieAudio(u32 a);
s32 StopMovieAudio(u32 a);
s32 GetMovieAudioHalf(u32 a);
s32 SetMovieAudioVolume(f32 level, f32 time, u32 a);
s32 StepMovieAudioVolume(u32 a);
u32 AllocMovieBlock(u32 size, u32 arena);
u32 AllocMovieWork(u32 size, u32 arena);
void InitMovieArena(u32 arena, u32 base, u32 size);
u32 AllocFromMovieArena(u32 arena, u32 size);
s32 CreateSmvReader(u32 buf, u32 size, u32 lsn, u32 dbp, u32 desc);
s32 UpdateSmvReader(s64 stop);

// ---- In-menu movie page (movie_page.cpp) -------------------------------------------------------
void UploadSubtitleGlyphs(u32 list, s32 index, u32 mb);
void DrawSubtitleText(u32 list, s32 index, u32 mb);
void FadeOutStreamSlots(f32 time);
void UploadMessageGlyphs16(u32 list, s32 index, s64 id, u64 dbp);
void SetMoviePageDisplay();
void SetMoviePlayDisplay();
void SetMenuDisplayAfterMovie();
s32 AreMovieStreamSlotsIdle();
bool StepMovieToEnd();
void SetMoviePageDisplayThunk();
void StartMenuMovie(s32 id, s32 x, s32 y);
void ResetMovieTimerThunk();
s32 UpdateMenuMovie();
void MovieFrameSync();
void SetMenuDisplayAfterMovieThunk();
void DrawMovieLineBlend(u32 list, s32 index, s64 alpha);
void SetSubtitleLine(u32 mb, u32 entry);
void UpdateSubtitles(f32 t, u32 obj);
u32 CreateMenuMoviePage();
void StartPageMovie(u32 obj);
void HandleMenuMovieInput(u32 obj);
void DrawMovieWidescreen(u32 list, s32 index);
void DrawMenuMoviePage(u32 obj);
void MenuMovieEnter();
void MenuMovieWait();
void MenuMoviePlay();
void MenuMovieEnd();

// ---- Interlude viewer (interlude_page.cpp) -----------------------------------------------------
void UpdateInterludeHelp(u32 obj);
void UpdateInterludePreview(u32 obj);
void SetInterludeCursor(u32 obj, u64 i);
void HandleInterludeInput(u32 obj);
u32 CreateInterludePage();
void UpdateInterludePage(u32 obj);
void DrawInterludeTexts(u32 list, s32 index, u32 obj);
void DrawInterludePreview(f32 level, u32 list, s32 index, u32 obj, u64 n);
void DrawInterludePage(u32 obj);
void EnterInterludePage();
void LoadInterludePage();
void RunInterludePage();

// ---- Aircraft data tables (aircraft_data.cpp) --------------------------------------------------
u32 GetSpecialWeaponCode(u32 n);
u32 GetSpecialWeaponName(u32 n);
u32 GetAircraftCode(u32 n);
u32 GetAircraftLoadout(s64 a);
u32 GetAircraftLoadoutEntry(s64 a, s32 i);
u32 GetAircraftSpecialWeapons(s64 a);

// ---- Briefing (briefing_page.cpp) ---------------------------------------------------------------
u8 GetCampaignMissionNumber(u64 id);
void SetBriefingSubtitle(u32 mb, u32 entry);
void UpdateBriefingSubtitles(f32 time, u32 obj);
void DrawBriefingSubtitle(u32 list, s32 index, u32 obj);
void StartBriefingMovie(u32 obj);
u32 CreateBriefing();
void BriefingInput(u32 obj);
void BriefingUpdate(u32 obj);
void DrawBriefingHeader(u32 list, s32 index, u32 obj);
void DrawBriefingMoviePage(u32 obj);
void SelectBriefingCameraPreset(u32 obj, s32 n);
void InitBriefingMap(u32 obj);
void EnterBriefingMap(u32 obj);
void UpdateBriefingMapCamera(u32 obj);
void BriefingMapUpdate(u32 obj);
void DrawBriefingMapTexts(u32 list, s32 index, u32 obj);
u32 WriteBriefingUnitIcon(u32 p, u32 xy, s32 type, s32 faction);
u32 WriteBriefingUnitLink(u32 p, u32 xy0, u32 xy1, s32 faction);
void DrawBriefingUnits(u32 list, s32 index, u32 obj);
void DrawBriefingMapFrame(u32 list, s32 index, u32 obj);
void DrawBriefingMapPage(u32 obj);
void EnterBriefing();
void LoadBriefing();
void RunBriefingMovie();
void EndBriefingMovie();
void RunBriefingMap();
void InitTerrainMap(u32 map, u32 data);
u32 WriteTerrainPoints(u32 p, u32 map);
void DrawTerrainMap(u32 list, s32 index, u32 map, u32 image);
void UpdateTerrainMapCamera(u32 map);

// ---- Campaign menu (campaign_menu_page.cpp) ----------------------------------------------------
void UpdateCampaignMenuHelp(u32 obj);
u32 CreateCampaignMenu();
void LeaveCampaignMenu(u32 obj, u16 page);
void LeaveCampaignMenuToSubPage(u32 obj, u16 sub);
void CampaignMenuSelect(u32 obj, u32 tree);
void CampaignMenuUpdate(u32 obj);
void DrawCampaignMenuPage(u32 obj);
void EnterCampaignMenu();
void LoadCampaignMenu();
void CampaignMenuRun();

// ---- Camera library (camera.cpp) ---------------------------------------------------------------
void UpdateCameraMatrices(u32 cam);
void ResetCameraLookAt(u32 look);
void GetDirectionAngles(u32 out, u32 dir);
void SetCameraLookAt(u32 cam, u32 look);
void BuildClipMatrix(u32 m, f32 w, f32 h, f32 d, f32 near_z, f32 far_z);
f32 GetVectorLength(u32 v);

// ---- Mission title card (mission_title_page.cpp) ----------------------------------------------
u32 WriteDescriptorSprite(u32 p, u32 desc);
void DrawLetterboxBars(f32 level, u32 list, s32 index, s64 top, s32 bottom);
void InitMissionTitle(u32 obj, s32 state);
void CreateMissionTitle(s32 state);
void MissionTitleUpdate(u32 obj);
void DrawMissionTitleImage(f32 level, u32 list, s32 index, u32 image);
void DrawMissionTitlePage(u32 obj);
void EnterMissionTitle();
void LoadMissionTitle();
void MissionTitleRun();

// ---- Options (options.cpp) ---------------------------------------------------------------------
void ApplyOptionSettings();
void func_001053C8(u32 block);
void ResetCampaignData();
void ResetScreenPosition(u32 p);
void ResetBrightness(u32 p);
void ClampStatistics(u32 p);
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
s32 func_001025C8(u32 pad, u8 on, u8 strength);
s32 TickPadVibration(u32 pad);

// ---- 2D packets, textures, fade, 2D camera, containers (render2d.cpp) ---------------------------
u32 AllocQwords(s32 qwords);
void AllocDoubleBuffer(u32 db, s32 qwords);
u32 CloseDmaPacket(u32 tag, u32 end);
void InitBucketList(u32 list, u32 count);
u32 BeginPacket();
u32 EndPacket(u32 list, s32 index, u32 end);
u32 LinkPacket(u32 list, s32 index, u32 start, u32 end);
u32 SetPacketListBuckets(u32 packet_list, u32 list);
void InitGimTexture(u32 tex, u32 gim);
void SetTextureVramAddress(u32 tex, u16 tbp, u16 cbp);
u32 BuildTextureUpload(u32 tex, u32 p, s32 ctx_mode, u32 clut_loads);
u64 GetTextureTex0(u32 tex);
u32 BuildSpriteStrips(u32 p, u64 a, u64 b);
void BuildFadeQuad(u32 list, s32 index, s64 mode, u32 alpha, u64 a, u64 b);
void DrawScreenFade(f32 fade, u32 list, s32 index, s64 mode, u32 cam);
void Init2dCamera(u32 cam);
void SetCameraProjection(f32 width, f32 height, f32 distance, f32 aspect, f32 near_z, u32 cam);
void SetCameraViewport(f32 z_near, f32 z_far, u32 cam, s16 x, s16 y, s16 w, s16 h);
void InitContainerReader(u32 reader, u32 container);
u32 GetContainerItem(u32 reader, s32 index);
u32 WriteDmaCallTag(u32 p, u32 target);
u64 LerpColor(f32 t, u64 a, u64 b);
void func_001050D8(f32 t, u32 out, u32 a, u32 b);
u32 BuildSpriteStripsUv(u32 p, u64 xy0, u64 xy1, u64 uv0, u64 uv1);
u32 AllocBucketPacket(u32 list, s32 index, u32 qwords, u32 w2, u32 w3);
void AddBucketRef(u32 list, s32 index, u32 addr, u32 qwords);
void func_00104090(u32 list, s32 index, u32 sub);
u32 func_00104250(u32 list, s32 index, u32 w2, u32 w3);
u32 func_001042A0(u32 n);
u32 func_00104338(u32 list, s32 index, u32 target, u32 n);
void func_001043E8(u32 pl, u32 sub);
u32 WriteTex0Tex1(u32 tex, u32 p);
u32 BuildClippedSpriteStripsUv(u32 p, u64 xy0, u64 xy1, u64 uv0, u64 uv1, u64 clip0, u64 clip1);

// ---- Memory card probe (save_mc.cpp) -----------------------------------------------------------
s32 ProbeMcCardInfo(u32 block);
s32 ReadMcDir(u32 block, u32 path);
s32 CheckMcCard(u32 save, s32 port);
s32 CheckMcSaveDir(u32 save, s32 port);
s32 ReadMcFile(u32 block, u32 buf, u32 path, s32 size);
s32 FormatMcCard(u32 block);
s32 MakeMcDir(u32 block, u32 path);
s32 WriteMcFile(u32 block, u32 buf, u32 path, s32 size);
s32 DeleteMcFile(u32 block, u32 path);
void RegisterMcSaveFile(u32 save, s32 port, s32 index, u32 name, u32 buf, u32 size);
s32 GetMcStatus(u32 save, s32 port);
s32 FormatMcSaveCard(u32 save, s32 port);
s32 CreateMcSave(u32 save, s32 port);
s32 LoadMcSaveFile(u32 save, s32 port);
s32 OverwriteMcSaveFile(u32 save, s32 port);
s32 DeleteMcSave(u32 save, s32 port);

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
u32 GetSoundTransferStatus();
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
u32 func_00218ED0();
u16 func_00218F00(s32 n);
s32 func_00218B28(s32 n);
s32 func_00219798(u32 rec);
void func_0021A8D8(u32 a, u32 b, u32 c);
void func_0021AEE0();
void func_0021B030();
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
void ClearReverbDepths();
s32 func_0021D138(u32 rec);
s32 func_0021D680(s16 left, s16 right, u32 rec);
s32 StopSound(u32 rec);
s32 func_0021E240(u8 voice, s16 volume, u8 left_pan, u8 right_pan);
s32 func_002179D8(u32 cmd);
void func_002186C0(u32 cmd);
s32 PlayMenuSe(s16 bank, s16 sound);
void StopAllVoices();
void func_0021A988(s16 volume, s8 pan, u32 rec);
s8 func_0021AAC8(s8 pan, s16 offset);
void func_0021AB30(u32 snd);
s32 func_0021AC28(u32 snd);
s8 func_0021ACB8(u32 snd);
f32 func_0021CD08(s32 n);
u16 NextSoundRandom();
s16 func_0021CD48(s16 x);
u32 func_0021CDD0(u32 volume, s8 delta);
void func_0021CFE8(u8 voice);
void func_0021D018(u8 voice);
void func_0021D0C8(u32 rec);
s32 func_0021D188(s16 bank, s16 sound, u32 rec);
s32 func_0021D1C0(s16 bank, s16 sound, u32 rec, s8 priority);
s32 func_0021D500(s16 bank, s16 sound, s16 volume, s16 pan, u32 rec, s8 priority);
s32 func_0021E0E8(u32 mask_lo, u32 mask_hi);

// ---- Scene 6: startup (startup.cpp) ------------------------------------------------------------
void RequestSoundBankLoad();
void UpdateSoundBankLoad();
s32 UpdateStartupWarnings();
void EvaluateCardResult();
void func_002603D8();
void UpdateCardMessages();
void DrawCardMessages();
void StartupSplashUpdate();
void DrawSplashImage();
void InitStartupScene();
void LoadStartupData();
void SetupStartupData();
void InitStartupCamera();
void ShowStartupWarnings();
void PrepareCardProbe();
void ProbeStartupCard();
void RunStartupCardMessages();
void StartStartupSplash();
void RunStartupSplash();
void SceneStartup();
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
void func_0021E798(s32 slot, s32 mode);
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
void func_0021FB70(f32 time);
void func_0021FC20();
s32 func_0021FD40(u32 slot, u16 id);
s32 RequestStreamPlayArg(u32 slot, u16 id, u16 arg);
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


// ---- Model library (model.cpp) -----------------------------------------------------------------
f32 GetDistanceXZ(u32 a, u32 b);
void BuildScaleMatrix(f32 sx, f32 sy, f32 sz, u32 m);
f32 EvalHermite(f32 t, f32 a, f32 b);
void ClampVector(u32 v, u32 lo, u32 hi);
void BuildRotationMatrix(u32 full, u32 rotation, u32 rot, u32 trans);
void RotateMatrix(u32 out, u32 m_in, u32 rot);
void MulMatrixVu0(u32 out, u32 a, u32 b);
void SetUnitMatrixVu0(u32 m);
void CountModelNodes(u32 rec, u32 node, u32 count, u32 max_channel, u32 max_part);
void BindModelNodes(u32 rec, u32 node, u32 index);
void BindModelData(u32 rec, u32 mdl);
void CreateModelInstance(u32 inst, u32 rec);
void InterpolatePartPose(u32 inst, s32 idx, u32 rot_out, u32 trans_out);
void BuildMorphMesh(u32 inst, s32 idx);
void UpdateModelHierarchy(u32 inst, u32 index, u32 parent_m, u32 parent_n, u32 flags);
void SetModelPartValue(f32 v, u32 inst, s32 channel);
void func_00140408(u32 inst, u64 mask);
u32 func_00141000(u32 inst, s32 n);
void func_00141040(u32 inst, s32 id, s32 on);
u32 EmitModelInstance(u32 inst, u32 p, u32 cam, u32 world, u32 light, u32 mode);

// ---- Menu 3D objects and light sets (menu_object.cpp) ------------------------------------------
void InitLightSet(u32 ls);
void SetLight(u32 ls, u32 dir, u32 rgba, s32 index);
void SetAmbient(u32 ls, u32 rgba);
void SetLightTexture(u32 ls, u32 gim, u16 tbp, u16 cbp);
void WriteLightSetPacket(u32 ls, u32 cam, u32 list, s32 index, u32 flags);
void InitMenuObject(u32 obj, s32 mode);
void BindMenuObject(u32 obj, u32 model, u32 gim, u32 glow_model, u32 effects);
void SetMenuObjectTexturePos(u32 obj, u16 tbp, u16 cbp);
void SetMenuObjectPart(f32 v, u32 obj, s32 channel);
void UpdateMenuObject(u32 obj);
void SetMenuObjectPose(u32 obj, u32 rot, u32 trans);
void SetMenuObjectMatrices(u32 obj, u32 rotation, u32 full);
void UploadMenuObjectProgram(u32 obj, u32 list, s32 index);
void WriteMenuObjectTexture(u32 obj, u32 list, s32 index, u32 flags);
void WriteMenuObjectCamera(u32 obj, u32 cam, u32 list, s32 index, u32 vec);
void DrawMenuObject(u32 obj, u32 cam, u32 list, s32 index, u32 effect_list);

// ---- Hangar (hangar.cpp) -----------------------------------------------------------------------
void CreateMenuObject(u32 obj, u32 file, u32 item, s32 mode);
void InitLights(u32 lights, u32 ambient);
void SetLightDirection(f32 rx, f32 ry, u32 lights, u32 index, u32 rgba);
void ApplyLights(u32 ls, u32 lights);
void ApplyLightsTransformed(u32 ls, u32 lights, u32 m);
void BuildShadowMatrix(u32 m, u32 cam_pos, u32 pos);
void BuildModelMatrices(u32 rotation, u32 full, u32 rot, u32 trans);
void UpdateFreeCamera(u32 look, u32 preset);
void ClampFreeCamera(u32 look);
void ClampFreeCameraSpecial(u32 look);
void UpdateViewerCamera(f32 t, u32 look, u32 preset, u32 jump);
void UpdateHangarCamera(u32 look, u32 h);
void DrawSpecialIcons(u32 list, s32 index, s32 count, u32 mask);
void DrawHangarGauges(u32 list, s32 index, s32 count, u32 levels);
u32 DrawHangarSpecialText(f32 head, f32 body, u32 aircraft, u32 slot, s32 none);
u32 DrawHangarNameText(f32 head, f32 body, u32 aircraft);
u32 DrawHangarStatsText(f32 head);
u32 DrawHangarWeaponText(f32 head, f32 body, u32 aircraft, u32 slot, s32 none);
u32 DrawHangarColourText(f32 head, f32 body, u32 owned, s32 none);
u32 DrawHangarOwnedMarks(f32 t, u32 aircraft, u32 rows);
u32 DrawHangarSpecialMarks(f32 t, u32 aircraft, s32 slot, u32 rows);
u32 DrawHangarPriceText(f32 head, f32 body, s32 page, s32 credits, s32 change, s32 short_of_credits);
void DrawPriceBox(f32 t, u32 list, s32 index, s32 red);
u32 DrawHangarTexts(u32 list, s32 index, u32 h, u32 parts);
void DrawHangarPanels(u32 list, s32 index, u32 h);
s32 CheckHangarSaleHint(u32 h);
void UpdateHangarHelp(u32 v, u32 h);
void PurchaseStateToList(u32 dst, u32 src);
void PurchaseStateToSave(u32 dst, u32 src);
void SetHangarPageForSub(u32 h);
u64 GetStateMask(u64 row, u64 state);
void BuildAvailabilityMasks(u32 mode, u32 rows, u32 masks);
s32 ListSetBits(u32 list, s32 n, u64 bits);
u32 SelectFirstAvailable(u32 h);
u32 SelectAvailableSpecial(u32 h);
void MoveHangarCrosshair(u32 c, s32 page, s32 aircraft, s32 colour, s32 slot);
void ClearValues(s32 n, u32 values);
void ApproachValues(s32 n, u32 values, u32 targets);
s32 GetSpecialWeaponCount(s32 aircraft);
void CommitHangarSelection(u32 h);
s32 BuildPanelList(u32 flags, u32 dst);
void OpenHangarPage(u32 h);
void SetHangarPhase(u32 h, s32 phase);
void AnimateHangarPanels(u32 h);
u32 GetAircraftArrows(u32 h);
u32 GetSpecialArrows(u32 h);
void ApplyHangarChange(u32 h, u32 changes);
s32 CountOwnedFields(u32 rows);
s32 GetPurchasePrice(u32 rows, s32 kind, s32 pos, s32 index, s32 colour, s32 slot, s32 credits, s32 apply);
void HangarAircraftInput(u32 h, u32 repeat, u32 held);
void HangarSpecialInput(u32 h, u32 repeat);
void HangarAccept(u32 h);
void HangarCancel(u32 h);
void HangarInput(u32 h);
void HangarConfirmInput(u32 h);
void HangarUpdate(u32 h);
void PlayHangarBgm(u32 sub);
void InitHangarPanels(u32 h);
void InitHangarState(u32 h, u16 player, u32 aircraft, u32 colour);
void HangarEnter();
void SyncHangarViewer(u32 v, u32 h);
void UpdateHangar(u32 v, u32 h);
void DrawHangar(u32 v, u32 h);
void AircraftSelectRun();

// ---- Aircraft viewer (aircraft_viewer.cpp) -----------------------------------------------------
void SetViewerSpecialList(u32 v);
void InitAircraftViewer(u32 v, s32 aircraft, u32 colour);
void QueueViewerData(u32 v);
void QueueViewerAircraft(u32 v);
void QueueViewerSpecials(u32 v);
void UpdateViewerLoad(u32 v);
void CreateHangarObjects(u32 v);
void CreateAircraftRecord(u32 v);
void CreateAircraftColour(u32 v, u32 colour);
void CreateSpecialRecords(u32 v, s32 count);
void SetAircraftParts(f32 value, u32 rec, s32 colour, s32 aircraft, u32 mode);
void FinishAircraftObjects(u32 rec, s32 colour);
void WriteViewerPassState(u32 list, s32 index, u32 fba, s32 pass);
void DrawHangarRoom(u32 list, s32 index, u32 v);
void DrawHangarRoomGlass(u32 list, s32 index, u32 v);
void DrawAircraft(u32 list, s32 index, u32 v);
void DrawAircraftReflection(u32 list, s32 index, u32 v);
void DrawAircraftShadow(u32 list, s32 index, u32 v);
void DrawSpecials(u32 list, s32 index, u32 v, u32 mask);
void DrawSpecialReflections(u32 list, s32 index, u32 v, u32 mask);
void DrawSpecialShadows(u32 list, s32 index, u32 v, u32 mask);
void UpdateViewerCamera3D(u32 cam, u32 look);
void ClampViewerLookAt(u32 look);
void DrawCeilingLights(f32 y, f32 size, f32 alpha, u32 list, s32 index, u32 v);
void ClearViewerBackground(u32 list, s32 index);
void DrawViewerScene(u32 list, u32 v, u32 parts);
void UpdateAircraftViewer(u32 v);
void DrawAircraftViewer(u32 list, u32 v);
void SetAircraftViewerAircraft(u32 v, s32 aircraft, u32 colour);
void ResetViewerLookAt(u32 look);
u32 CreateAircraftViewer(s32 aircraft, u32 colour);
void SetAircraftViewerSelection(u32 v, s32 aircraft, u32 colour);

// ---- Mission select (mission_select.cpp) -------------------------------------------------------
void UpdateMissionSelectHelp(u32 p);
void GetMissionCounts(u32 p, s32 kind);
void SelectMissionRow(u32 p, u32 n);
void InitMissionList(u32 p);
void InitMissionSettings(u32 p);
u32 CreateMissionSelectPage();
void LerpFrame(f32 t, u32 out, u32 a, u32 b);
void ResetMissionFrame(u32 frame);
s32 AnimateMissionFrame(f32 t, u32 out, u32 p, s32 index);
void AcceptMission(u32 p);
void MissionListInput(u32 p);
void UpdateMissionPreview(u32 p);
void UpdateMissionList(u32 p);
void LeaveMissionSelect(u32 p, u16 target);
void LeaveMissionSelectMenu(u32 p, u16 kind, u16 arg);
void MissionSettingsCancel(u32 p, u32 tree);
void MissionSelectAction(u32 p, u32 tree);
void MissionSelectUpdate(u32 p);
void DrawMissionRecords(u32 list, s32 index, u32 p);
void DrawMissionThumbnail(f32 level, u32 list, s32 index, u32 p, u32 kind, u32 n);
void DrawMissionFrame(u32 list, s32 index, u32 p, u32 rect);
void DrawMissionList(u32 p);
void DrawMissionSettingsTexts(u32 list, s32 index, u32 p, s32 y);
void DrawTitleBar(f32 t, u32 list, s32 index, s32 x, s32 y, s32 dx, s32 dy, u32 color);
void DrawMissionSettings(u32 p);
void EnterMissionSelect();
void LoadMissionSelect();
void MissionSelectRun();

// ---- Scene 2: flight scene load path (flight_load.cpp) --------------------------------------
AC04_GUEST_VAR(u32, g_FlightScene, 0x004104C4)  // flight scene object, 0x1810 bytes
f32 func_0010A130(f32 target, f32 time, s32 p);
void func_0010A158();
void func_0010A228();
void func_0010A230();
void LoadFlightDataEntry();
void func_0010FDD8();
void EnterFlightSubState();
void func_0010A5D0();
void SceneFlight();
void CreateFlightScene();
void FlightSceneEnter();
void func_0010AD80();
void func_00239640();
void func_00239310(f32 alpha, u32 list, s32 index, s32 counter, s32 wait_sub);
void func_002396B0(f32 t, u32 list, s32 index, s32 counter, s32 blink);
void func_002399E0(u32 list, s32 index);
void func_00239B60(f32 alpha, u32 list, s32 index, s32 counter, s32 wait_sub);
void func_0010A998();
void StoreFlightDataPointers();
void func_0010FDE8();
void CreateFlightSystems();
void StartFlightSystems();
void func_0010C840();
void func_0010FA70();
void func_00118A18();
void func_0011A3D8(u32 r, u32 block);

// ---- Mission units (flight_units.cpp) -------------------------------------------------------
AC04_GUEST_VAR(u32, g_UnitList, 0x00410640)  // unit list header, 0x90 bytes
void func_00135C50();
void func_00133A80();
void func_00133A58();
void func_00134FA8();
void func_0011BA90();
void func_0011B948();
s32 func_0011E878(u32 id);
u8 func_0011B920(s64 a, s32 n);
void func_001279A0();
void func_001279B0(s32 p, s64 on);
void func_0011EC48(u32 pl, s64 k);
void func_0011E8D8(s32 i);
void func_0011EFA0(s64 i, s64 k);
void CreateAIAircraftState(s32 i);
void CreateGroundUnitState(s32 i);
u32 func_0010E3F8(u32 block);
void func_0010E5C0(u32 u, u32 r);
void SetupMissionUnits();

// ---- Flight systems: projectiles, HUD, replay, mission lists, pause menu (flight_systems.cpp) ---
void func_0014BC80();
void func_0014CBD0();
void func_0014DA98();
void func_0014DF40();
void func_0014E890();
void func_0014ECA0();
void func_0014F5B0();
void ResetProjectiles();
void CreateProjectilePools();
void func_00135D60();
void func_0015E818();
void func_0015E7B0();
void func_0015E8C0(u32 rows);
void func_0016DA60();
void func_0016DA78();
void func_0016DAE0();
void func_0016FEE8();
void func_00170460();
void func_0016DE20();
void func_0016DE48();
void func_0016DEF8();
void func_00176908();
void func_0017B2C8();
void func_0017B2F0();
void func_0017B708();
void func_0017B730();
void func_0017B868();
void func_0017B890();
void func_0017B978();
void func_0017B9A0();
void func_0017BB38();
void func_0017BC70();
void func_0017BF90();
void func_0017CBE0(u32 r);
void func_0017CCA8(u32 hud, u32 data);
void func_0017D8B0();
void func_0017E8B0(u32 item);
void func_0017E7D0(u32 list);
void func_0017DCC0();
void func_0017DC98();

// ---- Engine and ambient sound (engine_sound.cpp) --------------------------------------------
void func_002155D0(s32 p, u32 curves);
void func_00215838(s32 p, s32 n);
void func_00215BD0(u32 mode);
void func_00215CD0(s32 p, u32 curves, u32 bank, u16 v);
void func_00215D08(u16 a, u16 b, u16 c, u16 d);
void func_002162F0();
void func_00216348();
void func_00216520(u32 mode);
void func_00216528();
void func_00216550(s32 n, u32 aircraft, u32 mask);
void func_00216570(s32 n, u32 curves, u32 header);
void func_002165D0(u32 players);
void func_002166D0();
void func_00216728();
void func_00216790(u32 mode);

// ---- Radio, vibration and chatter set-up (radio.cpp) ----------------------------------------
void func_002508E0();
void func_00253038();
void func_00254E48(u32 p);
void func_00254FB0(u32 r);
void func_002556A0();
void func_002583C0();
void func_00259280(u32 t);
void func_002509F8(u32 base, u32 b);
void func_00250C30(u32 db, u32 msg);
void func_00257430(u32 slot);
void func_00257468(s32 p, s32 n);
void func_00257630(u32 players);
void func_002594F0();

// ---- Flight view cameras and projectile follow (view_camera.cpp) ----------------------------
void func_00102838(u32 a, u32 b);
void func_00102990(u32 a, u32 b);
void func_0010C850(f32 a, f32 b, f32 c, u32 view, u32 d);
void func_0010C9D0();
void func_0010D5E8(u32 view, u8 flags);
void func_0010D5F0(u32 view, u32 eye, u32 target);
void func_0010D610(u32 view, u32 attach);
void func_0010D630(u32 view, u32 up);
void func_0010D650(u32 view, u32 a, u32 b);
void func_0010D680(u32 view, u32 look);
void func_0010D6A0(u32 view);
void func_0010D748(u32 view, u32 look);
void func_00136570(s32 n);
void func_00136518();
void func_001365A0();
void func_00136770(s32 n, s32 on);
void FollowProjectile(s32 n, s64 kind, s32 slot);

// ---- Terrain height queries and model-set getters (terrain_query.cpp) -----------------------
void func_0013D590(u32 out_cell, u32 out_offset, s32 x, s32 z);
f32 func_0013E338(u32 map, u32 pos);
f32 func_00146710(u32 set, s32 unit);
f32 func_00146788(u32 set, s32 unit);
void func_00149E28(u32 set, s32 p, u32 data);
void func_00149E90(u32 set, s64 n);

// ---- World render objects: terrain, sea, map entries (world_load.cpp) -----------------------
void func_001390D0(u32 t);
void func_00139520(u32 t);
void func_00137550();
void func_001381F8(u32 s);
void func_00139718(u32 t, u32 container, s32 n);
void func_00139038(u32 t, u32 rs);
void func_001390B0(u32 t, s32 n);
void func_00139FE8(u32 t);
void func_00139B70(u32 t);
void CreateWorldRenderObjects();
void func_0013EC20(u32 l);
void func_0013EC38(u32 l, u32 models, u32 count);
u32 func_00144228(u32 model);

// ---- Sky, stars, clouds and weather (sky.cpp) -----------------------------------------------
void func_00113548(u32 out, s32 n, u32 seed);
void func_001135C8(u32 out, s32 n, u32 seed);
void func_00113750(u32 rs);
void func_00113708(u32 rs);
void func_001117F8(u32 mission);
void func_001118C8();
void func_00111950();
f32 func_00110170(f32 yaw, u32 angles);
u64 func_001100A0(u32 angles, f32 yaw, f32 pitch);
void func_00112CA8(u32 list, s32 bucket, u32 cbp, u32 cam);
u32 func_00113B50(u32 p, u32 tex0, u32 fbp, s32 w, s32 h);
void func_001132E0(u32 list, s32 bucket, s32 a);
s32 func_0010FFB8(u32 out, u32 p);
u64 func_00110F38(u32 pts, u32 out);
u64 func_00110FE0(u32 p, u32 out);
u32 func_00111050(u32 pkt, u64 mask, u32 a, u32 b, u32 cols, u64 za, u64 zb);
u32 func_00111138(u32 pkt, u32 w, u32 buf, s32 z);
u32 func_00111310(u32 pkt, u32 w, u32 buf, u32 cam);
void func_00110240(u32 out, f32 pitch, f32 yaw);
void func_001102B0(u32 w, u32 unused, u32 cam);
void func_00110508(f32 p, u32 out, u32 g);
void func_00110630(f32 t, f32 p, u32 out, u32 g, u32 s);
void func_00110AA8(u32 w, u32 unused, u32 s, f32 t);
void func_001115C0(u32 cam, u32 list);
void func_001119A8(u32 k, u32 cam);
void func_00111B28(u32 cam, u32 list);
u32 func_00111C80(u32 p, s32 n, u64 scissor, u32 tex, u32 frames);
u32 func_00111EC8(u32 p, s32 n, u64 scissor, u32 tex, u32 frames, u64 cola, u64 colb);
void func_001120E8(f32 t, u32 list, s32 bucket, u32 cam, s32 levels, u32 colour);
void func_00112450(u32 list, s32 bucket, u32 cam, s32 levels);
u32 func_00113468(u32 p, u64 frame, u64 scissor, u64 xyoffset);
u32 func_00113988(u32 p, u32 fbp, s32 w, s32 h);
s32 func_00116060(u32 xyz, u32 st, u32 q, u32 p, u32 src, u32 z);
void func_00116130(u32 w, u32 cam, u32 s);
u32 func_00116320(f32 t, f32 c, u32 pkt, u32 w, u32 s);
void func_00116918(u32 cam, u32 list);
u32 func_0013EA08(u32 t);
f32 func_00115198(f32 x, f32 z);
f32 func_001152A8(f32 x, f32 z);
u32 func_00115CF8(u32 p, u32 cam, u32 wind, u32 colour);
void func_00115F28(u32 cam, u32 list);
void func_001145E8(u32 w, u32 cam, u32 s);
u32 func_00113790(f32 size, u32 p, u32 m, u32 pts);
u32 func_00114950(u32 p, u32 w, u32 fbp);
u32 func_00114C60(u32 p, u32 w, u32 fbp, u32 pts, u32 s, u32 cam);
f32 func_00114ED8(u32 p, u32 rec, u32 rect, u32 zmin, f32 size);
u32 func_001152D8(u32 p, u32 cam, u32 s, u32 c, s32 layers);
u32 func_00113D70(u32 p, u32 cam);
void func_00115B10(u32 cam, u32 list);
void func_00116A68(u32 out, u32 cam, f32 pitch, f32 yaw, f32 roll);
void func_00116B10(u32 out, u32 in, f32 sx, f32 sy);
void func_00116B58(u32 r, u32 cam);
u64 func_00116BF8(s32 n, u32 out, u32 pts, u32 m, u32 rect);
u32 func_00116CD8(u32 p, s32 n, u32 xy, u32 uv, u64 z);
void func_00116D38(f32 pitch, f32 yaw, u32 cam, u32 list);
u64 func_00116F30(u32 w, f32 tx, f32 ty, f32 size, f32 scale, f32 roll);
u32 func_001170F0(u32 p, u32 cam, u32 w, s32 a, s32 b, u64 colour);
void func_001177D8(u32 list, s32 bucket, u32 cam, u32 w, u64 colour);
void func_00117940(f32 pitch, f32 yaw, u32 cam, u32 list);
u32 func_00117E80(u32 p, u32 cam);
void func_00118330(u32 cam, u32 list, s32 bucket);
void func_00118400(f32 intensity, u32 cam, u32 list, s32 n);

// ---- Unit model sets and instances (unit_models.cpp) ----------------------------------------
void func_001457E0(u32 obj, u32 set, u32 container);
void func_001459E0(u32 obj);
void func_001456F8(u32 out, u32 d);
void func_00145D08(f32 v, u32 h, s32 u, s32 track);
u32 func_00145DA0(u32 h, s32 u, s32 track);
void func_00146680(u32 h, u32 out, s32 u, s32 id);
u32 func_00146988(u32 h, s32 u);
void func_001359D0(s32 u);
void UpdateUnitModelHierarchies(u32 h);
void SelectUnitModelLods(u32 h, u32 v);
void func_00146268(u32 h, u32 cam, u32 ot, u32 ot2);

// ---- Static map objects (static_objects.cpp) ------------------------------------------------
s32 func_00143C10(u32 inst, u32 out, u32 matrix);
void func_00144CC0(u32 obj);
void func_00144728(u32 obj);
void func_00145010(u32 o, u32 cam, u32 ot);
void func_00145578(u32 o, u32 cam, u32 ot);
void DrawMapObjects(u32 cam, u32 ot);

// ---- Aircraft models (aircraft_models.cpp) --------------------------------------------------
void func_00149FB8(u32 obj);
void func_001481B8(u32 a);
void func_00148308(u32 a, s32 n);
void func_00146AB0(u32 ot, s32 b, u32 tbp, s32 w, s32 h, s32 fbp);
void func_00146E28(u32 ot, s32 b, u32 tbp, s32 w, s32 h, s32 fbp, u32 fix, s32 du, s32 dv);
void func_001471C8(u32 ot, s32 b, s32 fbp, s32 w, s32 h, u32 tbp, u32 colour, s32 mode);
void func_00147540(u32 ot, s32 b, s32 fbp, s32 w, s32 h);
void func_00147828(u32 ot, s32 b, s32 fbp, s32 w, s32 h, u32 colour, u32 mask);
void func_00147B30(u32 ot, s32 b, u32 page, s32 w, s32 h);
void func_0014A500(u32 a, u32 ot);
void func_00149AC8(u32 a, u32 m, s32 id, s32 n);
void func_00149EC8(u32 m, s32 n, s32 id, u32 player);
void func_0014A588(u32 mdl, u32 v, u32 ot, s32 k, s32 lod0);
void func_0014AA78(u32 mdl, u32 cam, u32 ot, u32 angles, u32 pos, s32 slot, s32 lod);
void func_0014ACB0(u32 mdl, u32 cam, u32 ot, u32 angles, u32 pos, s32 slot, s32 lod, s32 idx);
void func_001483A0(u32 a, u32 v, u32 ot, s32 k, u32 ot2);
void func_0014C310(u32 cam, u32 ot);
void func_0011E0D8(u32 v, u32 ot, u32 ot2);
s32 DrawAircraftShadow(u32 inst, u32 cam, u32 ot, s32 b, u32 pos, u32 ang, u32 l, s32 mode);
void func_00147BF0(u32 obj);

// ---- Aircraft model animation (aircraft_models.cpp) -----------------------------------------
void func_00149C40(f32 v, u32 models, s32 n, s32 channel);
void func_00149D60(u32 models, s32 n, s32 node, s32 on);
void func_00149E38(u32 models, s32 n, u32 point, u32 value);
void func_00149EB8(s32 channel, s32 n, f32 v);
void func_0014AA38(u32 models, s32 n, s32 which, s32 idx, u32 value);
void UpdateAircraftMovingParts(s32 i, s32 reset);

// ---- Segment collision: terrain and static objects (collision.cpp) --------------------------
void func_0017BFD0(u32 out, u32 a, u32 b, u32 c);
s32 func_0017C250(u32 p0, u32 p1, u32 p2, u32 from, u32 to, u32 hit, u32 n);
s32 func_0017C4E8(u32 p0, u32 p1, u32 p2, u32 from, u32 to, u32 hit, u32 normal);
s32 func_00143EF8(u32 tris, s32 count, u32 hit, u32 from, u32 to, u32 normal);
s32 func_00144DC8(u32 obj, u32 hit, u32 from, u32 to, s32 x, s32 z, u32 normal, u32 attr);
s32 func_0013D698(u32 map, u32 hit_out, u32 attr_out, u32 from, u32 to, u32 normal_out);

// ---- View camera solve (view_camera.cpp) ----------------------------------------------------
void func_0010D760(u32 v);

// ---- View camera update (view_camera.cpp) ---------------------------------------------------
void func_00175B08(u32 v);
void UpdateViewCameras();

// ---- Death camera (view_camera.cpp) ---------------------------------------------------------
void func_0010C868(f32 theta, s32 p);

// ---- Player view control (view_camera.cpp) --------------------------------------------------
void func_00125B70(s32 p);

// ---- Rumble shake factor (radio.cpp) --------------------------------------------------------
f32 func_00257360(s32 p);

// ---- VU0 visibility helpers (vu0_clip.cpp) --------------------------------------------------
void func_001367B0(u32 m);
s32 func_001367C8(u32 p, u32 out);
s32 func_00136818(u32 a, u32 b, u32 out);

// ---- World drawing helpers (world_draw.cpp) -------------------------------------------------
void func_00136990(f32 near_off, f32 far, u32 m, u32 cam);
void func_00136B68(f32 rx, f32 ry, u32 ls, u32 rgba, s32 n);
u32 func_00137318(u32 ls, u32 out);
s32 func_0013A538(u32 c, u32 r, u32 p, u32 out);
void func_0013A5C8(u32 map, u32 out, s32 x, s32 z);
void func_00138EE8(u32 out_block, u32 out_offset, s32 x, s32 z);
void func_00138F70(u32 out_block, u32 out_offset, s32 x, s32 z);
s32 func_00139000(f32 v);

// ---- Terrain per-view setup (world_draw.cpp) ------------------------------------------------
void func_0013A110(u32 t, u32 cam);

// ---- Terrain culling (world_draw.cpp) -------------------------------------------------------
void func_0013A6B8(u32 t, u32 cam);
void func_0013B2F8(u32 t, u32 cam, u32 ot);
void func_0013C1D0(u32 t, u32 ot, u32 blk, u32 idx, s32 x, s32 z, u32 a, u32 cls);
void func_0013C740(u32 t, u32 ot, u32 blk, u32 idx, s32 x, s32 z, u32 a, u32 cls);
void func_0013CDC0(u32 t, u32 cam, u32 ot);

// ---- Sea and view packet helpers (world_draw.cpp) -------------------------------------------
u32 func_00137930(u32 buf, u32 n_out, u32 mc);
void func_00138350(u32 w, u32 cam, u32 ot);
void func_00138B50(u32 ot);
void func_00138BB0(u32 ot);
void func_00138C10(u32 ot);
void func_00138C28(u32 ot);
void func_00138C58(u32 cam, u32 ot);
void func_00138D18(u32 ot);
void func_00138DB0(u32 cam, u32 ot);
void func_0013ED58(u32 cam, u32 ot, s32 b, u32 a);
void DrawTerrain(u32 cam, u32 ot, u32 ot2);
void func_00155AF0(u32 ot);
void BuildFlightDrawLists();
void func_0010BA98();
void func_0013EC60(u32 list, u32 cam, u32 ot);

}  // namespace ac04::game
