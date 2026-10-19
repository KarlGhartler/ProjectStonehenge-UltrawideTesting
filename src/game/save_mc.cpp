// Memory card probe (docs/subsystems/save.md, docs/subsystems/results-startup.md sub 6): the libmc
// request state machines of a port block, polled once per frame with sceMcSync(1, ...).
//
// Port block (0x540 bytes at save + 0x40 + port * 0x540): +0x000 directory table (18 entries of
// 0x40), +0x480 port, +0x484 slot, +0x488 card type, +0x48C free clusters, +0x490 format,
// +0x494 last result, +0x498 directory entry count, +0x4A0 request step, +0x4A4 status,
// +0x4A8 directory name ("/BASLUS-20152AC04").
#include "game/game.h"

#include "common/guest_stack.h"
#include "sce/sce.h"

namespace ac04::game {

using guest::GRef;

namespace {

constexpr u32 kMcCmd = 0x004118FC;    // sceMcSync command output
constexpr u32 kMcPath = 0x004C34B0;   // path buffer for sceMcGetDir
// GetInfo consistency check (0x004118A8..0x004118CC).
AC04_GUEST_VAR(s32, g_McInfoTimeout, 0x004118A8)
AC04_GUEST_VAR(s32, g_McInfoRepeat, 0x004118AC)
AC04_GUEST_VAR(s32, g_McInfoResult, 0x004118B0)
AC04_GUEST_VAR(s32, g_McInfoType, 0x004118B4)
AC04_GUEST_VAR(s32, g_McInfoPrevType, 0x004118B8)
AC04_GUEST_VAR(s32, g_McInfoFree, 0x004118BC)
AC04_GUEST_VAR(s32, g_McInfoPrevFree, 0x004118C0)
AC04_GUEST_VAR(s32, g_McInfoFormat, 0x004118C4)
AC04_GUEST_VAR(s32, g_McInfoPrevFormat, 0x004118C8)
AC04_GUEST_VAR(s32, g_McDirTimeout, 0x004118CC)
// File operation state (S2b): poll counters, file descriptors and "failed, closing" flags.
AC04_GUEST_VAR(s32, g_McReadTimeout, 0x004118D0)
AC04_GUEST_VAR(s32, g_McReadFd, 0x004118D4)
AC04_GUEST_VAR(s32, g_McReadFailed, 0x004118D8)
AC04_GUEST_VAR(s32, g_McFormatTimeout, 0x004118DC)
AC04_GUEST_VAR(s32, g_McUnformatTimeout, 0x004118E0)
AC04_GUEST_VAR(s32, g_McMkdirTimeout, 0x004118E4)
AC04_GUEST_VAR(s32, g_McWriteTimeout, 0x004118E8)
AC04_GUEST_VAR(s32, g_McWriteFd, 0x004118EC)
AC04_GUEST_VAR(s32, g_McWriteFailed, 0x004118F0)
AC04_GUEST_VAR(s32, g_McDeleteTimeout, 0x004118F4)
AC04_GUEST_VAR(s32, g_McChdirTimeout, 0x004118F8)

constexpr u32 kMcFilePaths = 0x004C2EB0;  // 0x100 bytes per registered file path

}  // namespace

// EE: 0x0025E3A8
// Card info: repeats sceMcGetInfo until the same type/free/format is returned three times in a
// row (results 0, or -1/-2 for a new card, count as the same). Returns 1 when done, -1 on error or
// after 180 frames without completion, 0 while running.
s32 ProbeMcCardInfo(u32 block) {
    guest::GuestFrame frame(0x10);
    const u32 result_addr = frame.Address();
    s32 ret = 0;
    const s32 step = GRef<s32>(block + 0x4A0);
    bool issue = false;
    if (step == 1) {
        issue = true;
    } else if (step == 0) {
        g_McInfoType() = GRef<s32>(block + 0x488);
        g_McInfoFree() = GRef<s32>(block + 0x48C);
        g_McInfoPrevFormat() = 10;
        g_McInfoResult() = 10;
        g_McInfoRepeat() = 0;
        g_McInfoPrevType() = g_McInfoType();
        g_McInfoPrevFree() = g_McInfoFree();
        GRef<s32>(block + 0x4A0) = 1;
        GRef<s32>(block + 0x4A4) = 2;
        issue = true;
    } else if (step == 2) {
        if (sceMcSync(1, kMcCmd, result_addr) == 1) {
            const s32 r = GRef<s32>(result_addr);
            const s32 prev = g_McInfoResult();
            if ((prev == r || (prev == -1 && r == 0) || (prev == -2 && r == 0)) &&
                g_McInfoPrevType() == g_McInfoType() && g_McInfoPrevFree() == g_McInfoFree() &&
                g_McInfoPrevFormat() == g_McInfoFormat()) {
                g_McInfoRepeat() += 1;
                if (g_McInfoRepeat() < 3) {
                    GRef<s32>(block + 0x4A0) = 1;
                } else {
                    s32 res = g_McInfoResult();
                    if (res < -10) {
                        res = -11;
                        g_McInfoResult() = -11;
                        GRef<s32>(block + 0x498) = 0;
                    }
                    GRef<s32>(block + 0x494) = res;
                    if (res == 0 && g_McInfoPrevFormat() == 0) GRef<s32>(block + 0x494) = -2;
                    ret = 1;
                    GRef<s32>(block + 0x488) = g_McInfoPrevType();
                    GRef<s32>(block + 0x48C) = g_McInfoPrevFree();
                    GRef<s32>(block + 0x490) = g_McInfoPrevFormat();
                    GRef<s32>(block + 0x4A4) = 1;
                }
            } else {
                g_McInfoPrevType() = g_McInfoType();
                g_McInfoPrevFree() = g_McInfoFree();
                g_McInfoPrevFormat() = g_McInfoFormat();
                g_McInfoRepeat() = 0;
                g_McInfoResult() = r;
                GRef<s32>(block + 0x4A0) = 1;
            }
        } else {
            g_McInfoTimeout() += 1;
            if (g_McInfoTimeout() > 0xB4) {
                g_McInfoTimeout() = 0;
                GRef<s32>(block + 0x494) = -9999;
                ret = -1;
                GRef<s32>(block + 0x488) = -1;
                GRef<s32>(block + 0x4A4) = 4;
                GRef<s32>(block + 0x498) = 0;
            }
        }
    }
    if (issue) {
        g_McInfoTimeout() = 0;
        if (sceMcGetInfo(GRef<s32>(block + 0x480), GRef<s32>(block + 0x484), 0x004118B4, 0x004118BC, 0x004118C4) != 0) {
            GRef<s32>(block + 0x494) = -9999;
            GRef<s32>(block + 0x488) = -1;
            ret = -1;
            GRef<s32>(block + 0x4A4) = 3;
            GRef<s32>(block + 0x498) = 0;
        }
        GRef<s32>(block + 0x4A0) = 2;
    }
    if (ret != 0) GRef<s32>(block + 0x4A0) = 0;
    return ret;
}

// EE: 0x0025E5E8
// sceMcGetDir of `path` into the block's directory table (18 entries). Returns 1 with the entry
// count in +0x498, -1 on error or after 181 frames, 0 while running.
s32 ReadMcDir(u32 block, u32 path) {
    guest::GuestFrame frame(0x10);
    const u32 result_addr = frame.Address();
    s32 ret = 0;
    bool fail = false;
    s32 status = 0;
    bool poll = false;
    if (GRef<s32>(block + 0x4A0) == 0) {
        g_McDirTimeout() = 0;
        if (sceMcGetDir(GRef<s32>(block + 0x480), GRef<s32>(block + 0x484), path, 0, 0x12, block) == 0) {
            GRef<s32>(block + 0x4A4) = 2;
            GRef<s32>(block + 0x4A0) = 1;
            poll = true;
        } else {
            status = 3;
            GRef<s32>(block + 0x494) = -9999;
            GRef<s32>(block + 0x488) = -1;
            GRef<s32>(block + 0x4A4) = status;
            fail = true;
        }
    } else if (GRef<s32>(block + 0x4A0) == 1) {
        poll = true;
    }
    if (poll) {
        if (sceMcSync(1, kMcCmd, result_addr) != 1) {
            g_McDirTimeout() += 1;
            if (g_McDirTimeout() >= 0xB5) {
                g_McDirTimeout() = 0;
                GRef<s32>(block + 0x494) = -9999;
                GRef<s32>(block + 0x488) = -1;
                GRef<s32>(block + 0x4A4) = 4;
                fail = true;
            }
        } else {
            ret = 1;
            s32 r = GRef<s32>(result_addr);
            if (r >= 0) {
                GRef<s32>(block + 0x498) = r;
                GRef<s32>(block + 0x4A4) = 1;
            } else {
                if (r < -10) r = -11;
                GRef<s32>(block + 0x494) = r;
                GRef<s32>(block + 0x4A4) = r;
                fail = true;
            }
        }
    }
    if (fail) {
        ret = -1;
        GRef<s32>(block + 0x498) = 0;
    }
    if (ret != 0) GRef<s32>(block + 0x4A0) = 0;
    return ret;
}

// EE: 0x0025F438
// HYPOTHESIS: CheckMcCard. Card info of `port`: 1 = usable PS2 card, -1 = no card / unusable
// (status +0x4A4: 7 PS1/PocketStation card, -11 no card, -2 unformatted), 0 while running.
s32 CheckMcCard(u32 save, s32 port) {
    s32 ret = 0;
    const u32 block = save + static_cast<u32>(port) * 0x540 + 0x40;
    if (GRef<s32>(save + 4) == 0) {
        GRef<s32>(block + 0x4A4) = 0;
        GRef<s32>(save + 4) = 1;
    } else if (GRef<s32>(save + 4) != 1) {
        return 0;
    }
    const s32 r = ProbeMcCardInfo(block);
    if (r != 0) {
        const s32 type = GRef<s32>(block + 0x488);
        if (type == 1 || type == 3) {
            ret = -1;
            GRef<s32>(block + 0x4A4) = 7;
        } else if (GRef<s32>(block + 0x494) <= -11) {
            ret = -1;
            GRef<s32>(block + 0x4A4) = -11;
        } else if (GRef<s32>(block + 0x494) == -2) {
            GRef<s32>(block + 0x4A4) = -2;
            ret = -1;
        } else {
            ret = r == -1 ? -1 : (r == 1 ? 1 : 0);
        }
    }
    if (ret != 0) GRef<s32>(save + 4) = 0;
    return ret;
}

// EE: 0x0025F528
// Save directory check: card info, then sceMcGetDir of "/BASLUS-20152AC04" and, if it exists, of
// "/BASLUS-20152AC04/BASLUS-20152AC04". Returns 1 when done (+0x498 = entry count, or -1 when the
// directory exists without the save file), -1 on a card error, 0 while running.
s32 CheckMcSaveDir(u32 save, s32 port) {
    s32 ret = 0;
    const u32 block = save + static_cast<u32>(port) * 0x540 + 0x40;
    const u32 name = block + 0x4A8;
    const s32 step = GRef<s32>(save);
    bool check = false;
    if (step == 1) {
        check = true;
    } else if (step == 0) {
        const u64 args[1] = {name};
        ee::sprintf(kMcPath, 0x00411710, args, 1);  // "%s"
        GRef<s32>(block + 0x4A4) = 0;
        GRef<s32>(save) = 1;
        check = true;
    } else if (step == 2) {
        const s32 r = ReadMcDir(block, kMcPath);
        if (r == -1) {
            ret = -1;
        } else if (r == 1) {
            if (GRef<s32>(block + 0x498) != 0) {
                const u64 args[2] = {name, name};
                ee::sprintf(kMcPath, 0x00411700, args, 2);  // "%s%s"
                GRef<s32>(save) = 3;
            } else {
                ret = 1;
            }
        }
    } else if (step == 3) {
        const s32 r = ReadMcDir(block, kMcPath);
        if (r == -1) {
            ret = -1;
        } else if (r == 1) {
            if (GRef<s32>(block + 0x498) == 0) GRef<s32>(block + 0x498) = -1;
            ret = 1;
        }
    }
    if (check) {
        const s32 r = CheckMcCard(save, port);
        if (r == -1) ret = -1;
        else if (r == 1) GRef<s32>(save) = 2;
    }
    if (ret != 0) GRef<s32>(save) = 0;
    return ret;
}

// ---- File operations (S2b) ----------------------------------------------------------------------
// Each is a step machine in the block's +0x4A0, polled once per frame: 1 done, -1 failed (status in
// +0x4A4: 3 request refused, 4 timeout after 180 polls (300 for format), 5 short read/write,
// 10 card full, else the libmc result; +0x494 = -9999 on most failures), 0 running.

namespace {

void FailMcCard(u32 block) {
    GRef<s32>(block + 0x494) = -9999;
    GRef<s32>(block + 0x488) = -1;
}

}  // namespace

// EE: 0x0025E720
// Reads file `path` (`size` bytes) into `buf`: open (read only), read, close; one libmc request per
// frame. A short read or a timeout still closes the file and then fails.
s32 ReadMcFile(u32 block, u32 buf, u32 path, s32 size) {
    guest::GuestFrame frame(0x10);
    const u32 result_addr = frame.Address();
    s32 ret = 0;
    switch (GRef<s32>(block + 0x4A0)) {
        case 0:
            g_McReadTimeout() = 0;
            g_McReadFailed() = 0;
            if (sceMcOpen(GRef<s32>(block + 0x480), GRef<s32>(block + 0x484), path, 1) == 0) {
                GRef<s32>(block + 0x4A4) = 2;
                GRef<s32>(block + 0x4A0) = 1;
            } else {
                ret = -1;
                GRef<s32>(block + 0x4A4) = 3;
            }
            break;
        case 1:
            if (sceMcSync(1, kMcCmd, result_addr) == 1) {
                s32 r = GRef<s32>(result_addr);
                if (r < 0) {
                    GRef<s32>(block + 0x498) = 0;
                    if (r < -10) {
                        r = -11;
                        GRef<s32>(result_addr) = -11;
                    }
                    ret = -1;
                    GRef<s32>(block + 0x494) = r;
                    GRef<s32>(block + 0x4A4) = GRef<s32>(result_addr);
                } else {
                    g_McReadFd() = r;
                    GRef<s32>(block + 0x4A0) = 2;
                }
            } else {
                g_McReadTimeout() += 1;
                if (!(g_McReadTimeout() < 0xB5)) {
                    g_McReadTimeout() = 0;
                    GRef<s32>(block + 0x494) = -9999;
                    GRef<s32>(block + 0x4A0) = 4;
                    GRef<s32>(block + 0x488) = -1;
                    GRef<s32>(block + 0x4A4) = 4;
                    g_McReadFailed() = -1;
                }
            }
            break;
        case 2:
            g_McReadTimeout() = 0;
            if (sceMcRead(g_McReadFd(), buf, size) == 0) {
                GRef<s32>(block + 0x4A0) = 3;
            } else {
                GRef<s32>(block + 0x494) = -9999;
                GRef<s32>(block + 0x4A4) = 3;
                GRef<s32>(block + 0x4A0) = 4;
                GRef<s32>(block + 0x488) = -1;
                g_McReadFailed() = -1;
            }
            break;
        case 3:
            if (sceMcSync(1, kMcCmd, result_addr) == 1) {
                s32 r = GRef<s32>(result_addr);
                if (!(r < 0)) {
                    if (r == size) {
                        GRef<s32>(block + 0x49C) = r;
                        GRef<s32>(block + 0x4A0) = 4;
                    } else {
                        GRef<s32>(block + 0x4A4) = 5;
                        GRef<s32>(block + 0x4A0) = 4;
                        g_McReadFailed() = -1;
                    }
                    break;
                }
                GRef<s32>(block + 0x494) = -9999;
                GRef<s32>(block + 0x488) = -1;
                GRef<s32>(block + 0x498) = 0;
                if (r < -10) {
                    GRef<s32>(result_addr) = -11;
                    GRef<s32>(block + 0x4A4) = -11;
                } else if (r == -3) {
                    GRef<s32>(result_addr) = 10;
                    GRef<s32>(block + 0x4A4) = 10;
                } else {
                    GRef<s32>(block + 0x4A4) = r;
                }
                ret = -1;
            } else {
                g_McReadTimeout() += 1;
                if (!(g_McReadTimeout() < 0xB5)) {
                    g_McReadTimeout() = 0;
                    GRef<s32>(block + 0x494) = -9999;
                    GRef<s32>(block + 0x4A4) = 4;
                    GRef<s32>(block + 0x488) = -1;
                    GRef<s32>(block + 0x4A0) = 4;
                    g_McReadFailed() = -1;
                }
            }
            break;
        case 4:
            g_McReadTimeout() = 0;
            if (sceMcClose(g_McReadFd()) == 0) {
                GRef<s32>(block + 0x4A0) = 5;
            } else {
                FailMcCard(block);
                ret = -1;
                GRef<s32>(block + 0x4A4) = 3;
            }
            break;
        case 5:
            if (sceMcSync(1, kMcCmd, result_addr) == 1) {
                s32 r = GRef<s32>(result_addr);
                if (r == 0) {
                    ret = g_McReadFailed();
                    if (ret == 0) {
                        GRef<s32>(block + 0x4A4) = 1;
                        ret = 1;
                    }
                } else {
                    if (r < -10) {
                        r = -11;
                        GRef<s32>(result_addr) = -11;
                    }
                    FailMcCard(block);
                    ret = -1;
                    GRef<s32>(block + 0x4A4) = r;
                    GRef<s32>(block + 0x498) = 0;
                }
            } else {
                g_McReadTimeout() += 1;
                if (!(g_McReadTimeout() < 0xB5)) {
                    g_McReadTimeout() = 0;
                    FailMcCard(block);
                    ret = -1;
                    GRef<s32>(block + 0x4A4) = 4;
                }
            }
            break;
        default:
            break;
    }
    if (ret != 0) GRef<s32>(block + 0x4A0) = 0;
    return ret;
}

// EE: 0x0025EA60
// sceMcFormat of the block's card (300 polls before timing out; failure status 8).
s32 FormatMcCard(u32 block) {
    guest::GuestFrame frame(0x10);
    const u32 result_addr = frame.Address();
    s32 ret = 0;
    s32 step = GRef<s32>(block + 0x4A0);
    if (step == 0) {
        g_McFormatTimeout() = 0;
        if (sceMcFormat(GRef<s32>(block + 0x480), GRef<s32>(block + 0x484)) != 0) {
            GRef<s32>(block + 0x4A4) = 3;
            GRef<s32>(block + 0x4A0) = 0;
            return -1;
        }
        GRef<s32>(block + 0x4A4) = 2;
        GRef<s32>(block + 0x4A0) = 1;
        step = 1;
    }
    if (step == 1) {
        if (sceMcSync(1, kMcCmd, result_addr) != 1) {
            g_McFormatTimeout() += 1;
            if (300 < g_McFormatTimeout()) {
                g_McFormatTimeout() = 0;
                GRef<s32>(block + 0x494) = -9999;
                ret = -1;
                GRef<s32>(block + 0x488) = -1;
                GRef<s32>(block + 0x4A4) = 4;
            }
        } else {
            GRef<s32>(block + 0x498) = 0;
            GRef<s32>(block + 0x4A0) = 0;
            if (GRef<s32>(result_addr) == 0) {
                GRef<s32>(block + 0x4A4) = 1;
                ret = 1;
            } else {
                ret = -1;
                GRef<s32>(block + 0x4A4) = 8;
            }
        }
    }
    if (ret != 0) GRef<s32>(block + 0x4A0) = 0;
    return ret;
}

// EE: 0x0025EB58
// Dead (reached only from func_0025F820): sceMcUnformat of the block's card (1800 polls before
// timing out); the result code becomes the status on failure.
s32 UnformatMcCard(u32 block) {
    guest::GuestFrame frame(0x10);
    const u32 result_addr = frame.Address();
    s32 ret = 0;
    s32 step = GRef<s32>(block + 0x4A0);
    if (step == 0) {
        g_McUnformatTimeout() = 0;
        if (sceMcUnformat(GRef<s32>(block + 0x480), GRef<s32>(block + 0x484)) != 0) {
            GRef<s32>(block + 0x4A4) = 3;
            GRef<s32>(block + 0x4A0) = 0;
            return -1;
        }
        GRef<s32>(block + 0x4A4) = 2;
        GRef<s32>(block + 0x4A0) = 1;
        step = 1;
    }
    if (step == 1) {
        if (sceMcSync(1, kMcCmd, result_addr) != 1) {
            g_McUnformatTimeout() += 1;
            if (!(g_McUnformatTimeout() < 0x709)) {
                g_McUnformatTimeout() = 0;
                GRef<s32>(block + 0x494) = -9999;
                ret = -1;
                GRef<s32>(block + 0x488) = -1;
                GRef<s32>(block + 0x4A4) = 4;
            }
        } else {
            const s32 r = GRef<s32>(result_addr);
            GRef<s32>(block + 0x498) = 0;
            GRef<s32>(block + 0x4A0) = 0;
            if (r == 0) {
                GRef<s32>(block + 0x4A4) = 1;
                ret = 1;
            } else {
                GRef<s32>(block + 0x4A4) = r;
                ret = -1;
            }
        }
    }
    if (ret != 0) GRef<s32>(block + 0x4A0) = 0;
    return ret;
}

// EE: 0x0025EC58
// sceMcMkdir of `path`; the request and its first poll run in the same frame.
s32 MakeMcDir(u32 block, u32 path) {
    guest::GuestFrame frame(0x10);
    const u32 result_addr = frame.Address();
    s32 ret = 0;
    bool poll = false;
    if (GRef<s32>(block + 0x4A0) == 0) {
        g_McMkdirTimeout() = 0;
        if (sceMcMkdir(GRef<s32>(block + 0x480), GRef<s32>(block + 0x484), path) != 0) {
            ret = -1;
            GRef<s32>(block + 0x4A4) = 3;
        } else {
            GRef<s32>(block + 0x4A4) = 2;
            GRef<s32>(block + 0x4A0) = 1;
            poll = true;
        }
    } else if (GRef<s32>(block + 0x4A0) == 1) {
        poll = true;
    }
    if (poll) {
        if (sceMcSync(1, kMcCmd, result_addr) == 1) {
            s32 r = GRef<s32>(result_addr);
            GRef<s32>(block + 0x498) = 0;
            if (r == 0) {
                GRef<s32>(block + 0x4A4) = 1;
                ret = 1;
            } else {
                if (r < -10) r = -11;
                ret = -1;
                GRef<s32>(block + 0x4A4) = r;
                GRef<s32>(block + 0x494) = r;
            }
        } else {
            g_McMkdirTimeout() += 1;
            if (0xB4 < g_McMkdirTimeout()) {
                g_McMkdirTimeout() = 0;
                GRef<s32>(block + 0x494) = -9999;
                ret = -1;
                GRef<s32>(block + 0x488) = -1;
                GRef<s32>(block + 0x4A4) = 4;
            }
        }
    }
    if (ret != 0) GRef<s32>(block + 0x4A0) = 0;
    return ret;
}

// EE: 0x0025ED78
// Writes `size` bytes of `buf` to file `path` (created or truncated, mode 0x203): open, write,
// flush, close. Each request is polled in the frame it is issued. Errors after the open still go
// through the close (the result is then -1).
s32 WriteMcFile(u32 block, u32 buf, u32 path, s32 size) {
    guest::GuestFrame frame(0x10);
    const u32 result_addr = frame.Address();
    s32 ret = 0;
    auto fail_result = [&](s32 r) {  // libmc error result of open/write/flush
        GRef<s32>(block + 0x498) = 0;
        if (r < -10) {
            r = -11;
            GRef<s32>(result_addr) = -11;
        }
        ret = -1;
        GRef<s32>(block + 0x4A4) = r;
        GRef<s32>(block + 0x494) = GRef<s32>(result_addr);
    };
    auto timeout = [&]() {  // not finished after 180 polls: close the file
        g_McWriteTimeout() += 1;
        if (0xB4 < g_McWriteTimeout()) {
            g_McWriteTimeout() = 0;
            GRef<s32>(block + 0x494) = -9999;
            GRef<s32>(block + 0x4A4) = 4;
            GRef<s32>(block + 0x4A0) = 6;
            GRef<s32>(block + 0x488) = -1;
            g_McWriteFailed() = -1;
        }
    };
    auto refuse = [&](s32 status) {  // request refused: close the file
        GRef<s32>(block + 0x4A4) = status;
        GRef<s32>(block + 0x4A0) = 6;
        g_McWriteFailed() = -1;
    };
    s32 step = GRef<s32>(block + 0x4A0);
    if (!(static_cast<u32>(step) < 8)) return 0;
    if (step == 0) {
        g_McWriteTimeout() = 0;
        g_McWriteFailed() = 0;
        if (sceMcOpen(GRef<s32>(block + 0x480), GRef<s32>(block + 0x484), path, 0x203) != 0) {
            GRef<s32>(block + 0x4A4) = 3;
            GRef<s32>(block + 0x4A0) = 0;
            return -1;
        }
        GRef<s32>(block + 0x4A4) = 2;
        GRef<s32>(block + 0x4A0) = 1;
        step = 1;
    }
    switch (step) {
        case 1:
            if (sceMcSync(1, kMcCmd, result_addr) == 1) {
                const s32 r = GRef<s32>(result_addr);
                if (r < 0) {
                    fail_result(r);
                } else {
                    g_McWriteFd() = r;
                    GRef<s32>(block + 0x4A0) = 2;
                }
            } else {
                timeout();
            }
            break;
        case 2:
        case 3:
            if (step == 2) {
                g_McWriteTimeout() = 0;
                if (sceMcWrite(g_McWriteFd(), buf, size) != 0) {
                    refuse(3);
                    break;
                }
                GRef<s32>(block + 0x4A0) = 3;
            }
            if (sceMcSync(1, kMcCmd, result_addr) == 1) {
                const s32 r = GRef<s32>(result_addr);
                if (r < 0) {
                    fail_result(r);
                } else if (r == size) {
                    GRef<s32>(block + 0x49C) = r;
                    GRef<s32>(block + 0x4A0) = 4;
                } else {
                    refuse(5);
                }
            } else {
                timeout();
            }
            break;
        case 4:
        case 5:
            if (step == 4) {
                g_McWriteTimeout() = 0;
                if (sceMcFlush(g_McWriteFd()) != 0) {
                    refuse(3);
                    break;
                }
                GRef<s32>(block + 0x4A0) = 5;
            }
            if (sceMcSync(1, kMcCmd, result_addr) == 1) {
                const s32 r = GRef<s32>(result_addr);
                if (r == 0) {
                    GRef<s32>(block + 0x4A0) = 6;
                } else {
                    fail_result(r);
                }
            } else {
                timeout();
            }
            break;
        case 6:
        case 7:
            if (step == 6) {
                g_McWriteTimeout() = 0;
                if (sceMcClose(g_McWriteFd()) != 0) {
                    GRef<s32>(block + 0x4A0) = 0;
                    GRef<s32>(block + 0x4A4) = 3;
                    ret = -1;
                    break;
                }
                GRef<s32>(block + 0x4A0) = 7;
            }
            if (sceMcSync(1, kMcCmd, result_addr) == 1) {
                s32 r = GRef<s32>(result_addr);
                if (r == 0) {
                    GRef<s32>(block + 0x4A0) = 0;
                    if (g_McWriteFailed() != 0) {
                        ret = g_McWriteFailed();
                    } else {
                        GRef<s32>(block + 0x4A4) = 1;
                        ret = 1;
                    }
                } else {
                    GRef<s32>(block + 0x498) = 0;
                    if (r < -10) {
                        r = -11;
                        GRef<s32>(result_addr) = -11;
                    }
                    ret = -1;
                    GRef<s32>(block + 0x494) = r;
                    GRef<s32>(block + 0x4A4) = GRef<s32>(result_addr);
                }
            } else {
                g_McWriteTimeout() += 1;
                if (0xB4 < g_McWriteTimeout()) {
                    g_McWriteTimeout() = 0;
                    FailMcCard(block);
                    ret = -1;
                    GRef<s32>(block + 0x4A4) = 4;
                }
            }
            break;
        default:
            break;
    }
    if (ret != 0) GRef<s32>(block + 0x4A0) = 0;
    return ret;
}

// EE: 0x0025F050
// sceMcDelete of `path` (a file, or an empty directory); polled in the frame it is issued.
s32 DeleteMcFile(u32 block, u32 path) {
    guest::GuestFrame frame(0x10);
    const u32 result_addr = frame.Address();
    s32 ret = 0;
    bool poll = false;
    if (GRef<s32>(block + 0x4A0) == 0) {
        g_McDeleteTimeout() = 0;
        if (sceMcDelete(GRef<s32>(block + 0x480), GRef<s32>(block + 0x484), path) != 0) {
            ret = -1;
            GRef<s32>(block + 0x4A4) = 3;
        } else {
            GRef<s32>(block + 0x4A4) = 2;
            GRef<s32>(block + 0x4A0) = 1;
            GRef<s32>(result_addr) = 0;
            poll = true;
        }
    } else if (GRef<s32>(block + 0x4A0) == 1) {
        poll = true;
    }
    if (poll) {
        if (sceMcSync(1, kMcCmd, result_addr) == 1) {
            s32 r = GRef<s32>(result_addr);
            if (r == 0) {
                GRef<s32>(block + 0x4A4) = 1;
                ret = 1;
                GRef<s32>(block + 0x4A0) = 0;
            } else {
                if (r < -10) r = -11;
                ret = -1;
                GRef<s32>(block + 0x494) = r;
                GRef<s32>(block + 0x4A4) = r;
            }
        } else {
            g_McDeleteTimeout() += 1;
            if (0xB4 < g_McDeleteTimeout()) {
                g_McDeleteTimeout() = 0;
                GRef<s32>(block + 0x494) = -9999;
                ret = -1;
                GRef<s32>(block + 0x488) = -1;
                GRef<s32>(block + 0x4A4) = 4;
            }
        }
    }
    if (ret != 0) GRef<s32>(block + 0x4A0) = 0;
    return ret;
}

// EE: 0x0025F170
// Dead (no reference): sceMcChdir to `path` (180 polls before timing out); results below -10
// become -11.
s32 ChangeMcDir(u32 block, u32 path) {
    guest::GuestFrame frame(0x10);
    const u32 result_addr = frame.Address();
    s32 ret = 0;
    s32 step = GRef<s32>(block + 0x4A0);
    if (step == 0) {
        g_McChdirTimeout() = 0;
        const s32 r = sceMcChdir(GRef<s32>(block + 0x480), GRef<s32>(block + 0x484), path, 0);
        GRef<s32>(result_addr) = r;
        if (r != 0) {
            GRef<s32>(block + 0x4A4) = 3;
            GRef<s32>(block + 0x4A0) = 0;
            return -1;
        }
        GRef<s32>(block + 0x4A4) = 2;
        GRef<s32>(block + 0x4A0) = 1;
        step = 1;
    }
    if (step == 1) {
        const s32 sync = sceMcSync(1, kMcCmd, result_addr);
        if (sync == 1) {
            s32 r = GRef<s32>(result_addr);
            if (r == 0) {
                GRef<s32>(block + 0x4A4) = sync;
                ret = 1;
                GRef<s32>(block + 0x4A0) = 0;
            } else {
                if (r < -10) {
                    r = -11;
                    GRef<s32>(result_addr) = -11;
                }
                ret = -1;
                GRef<s32>(block + 0x494) = r;
                GRef<s32>(block + 0x4A4) = GRef<s32>(result_addr);
            }
        } else {
            g_McChdirTimeout() += 1;
            if (!(g_McChdirTimeout() < 0xB5)) {
                g_McChdirTimeout() = 0;
                GRef<s32>(block + 0x494) = -9999;
                ret = -1;
                GRef<s32>(block + 0x488) = -1;
                GRef<s32>(block + 0x4A4) = 4;
            }
        }
    }
    if (ret != 0) GRef<s32>(block + 0x4A0) = 0;
    return ret;
}

// EE: 0x0025F340
// Registers save file `index` of `port` (0..5; 5 = the main file named like the directory):
// builds its card path ("<dir>/<name>", 0x100 bytes per file at 0x004C2EB0; index 5 "<dir><dir>"
// at 0x004C33B0) and stores {path, buffer, size} in the file table (save +0xAC8, stride 0xC).
void RegisterMcSaveFile(u32 save, s32 port, s32 index, u32 name, u32 buf, u32 size) {
    const u32 dir = save + static_cast<u32>(port) * 0x540 + 0x40 + 0x4A8;
    u32 off;
    if (index == 5) {
        off = 0x500;
        const u64 args[2] = {dir, dir};
        ee::sprintf(kMcFilePaths + off, 0x00411700, args, 2);  // "%s%s"
    } else {
        off = static_cast<u32>(index) * 0x100;
        const u64 args[2] = {dir, name};
        ee::sprintf(kMcFilePaths + off, 0x00411708, args, 2);  // "%s/%s"
    }
    const u32 e = save + static_cast<u32>(index) * 0xC;
    GRef<u32>(e + 0xAC8) = kMcFilePaths + off;
    GRef<u32>(e + 0xAD0) = size;
    GRef<u32>(e + 0xACC) = buf;
}

// EE: 0x0025F420
// Status (+0x4A4) of `port`'s block.
s32 GetMcStatus(u32 save, s32 port) { return GRef<s32>(save + static_cast<u32>(port) * 0x540 + 0x4E4); }

// EE: 0x0025F6B8
// Formats the card of `port` when it is an unformatted PS2 card: card info, sceMcFormat, then the
// card check. Fails with status 7 (PS1/PocketStation card), -11 (no card) or 6 (already formatted).
s32 FormatMcSaveCard(u32 save, s32 port) {
    s32 ret = 0;
    const u32 block = save + static_cast<u32>(port) * 0x540 + 0x40;
    const s32 step = GRef<s32>(save);
    bool probe = false;
    if (step == 1) {
        probe = true;
    } else if (step == 0) {
        GRef<s32>(block + 0x4A4) = 0;
        GRef<s32>(save) = 1;
        probe = true;
    } else if (step == 2) {
        const s32 r = FormatMcCard(block);
        if (r == -1) {
            ret = -1;
        } else if (r == 1) {
            GRef<s32>(save) = 3;
        }
    } else if (step == 3) {
        const s32 r = CheckMcCard(save, port);
        if (r != 0) ret = r == -1 ? -1 : (r == 1 ? 1 : 0);
    }
    if (probe) {
        const s32 r = ProbeMcCardInfo(block);
        if (r != 0) {
            const s32 type = GRef<s32>(block + 0x488);
            s32 status = 0;
            if (type == 1 || type == 3) {
                status = 7;
            } else if (!(-0xB < GRef<s32>(block + 0x494))) {
                status = -0xB;
            } else if (r == -1) {
                ret = -1;
            } else if (r == 1) {
                if (GRef<s32>(block + 0x490) != 1) {
                    GRef<s32>(save) = 2;
                } else {
                    status = 6;
                }
            }
            if (status != 0) {
                ret = -1;
                GRef<s32>(block + 0x4A4) = status;
            }
        }
    }
    if (ret != 0) GRef<s32>(save) = 0;
    return ret;
}

namespace {

// Card check shared by CreateMcSave / LoadMcSaveFile / OverwriteMcSaveFile: a formatted PS2 card
// (type 2, result 0 or -1, format 1) moves on to step 2; anything else fails.
s32 CheckSaveCard(u32 save, s32 port, u32 block) {
    const s32 r = CheckMcCard(save, port);
    if (r == 0) return 0;
    if (r == -1) return -1;
    if (r != 1) return 0;
    if (GRef<s32>(block + 0x488) != 2) return -1;
    if (!(static_cast<u32>(GRef<s32>(block + 0x494) + 1) < 2)) return -1;
    if (GRef<s32>(block + 0x490) != 1) return -1;
    GRef<s32>(save) = 2;
    return 0;
}

}  // namespace

// EE: 0x0025F820
// Dead (no reference): unformats the card of `port` (step 0/1), then probes it (step 2). Returns 1
// once the card info probe succeeds, -1 on failure.
s32 func_0025F820(u32 save, s32 port) {
    s32 ret = 0;
    const u32 block = save + static_cast<u32>(port) * 0x540 + 0x40;
    const s32 step = GRef<s32>(save);
    if (step == 0 || step == 1) {
        if (step == 0) {
            GRef<s32>(block + 0x4A4) = 0;
            GRef<s32>(save) = 1;
        }
        const s32 r = UnformatMcCard(block);
        if (r == -1) ret = -1;
        else if (r == 1) GRef<s32>(save) = 2;
    } else if (step == 2) {
        const s32 r = ProbeMcCardInfo(block);
        if (r == -1) ret = -1;
        else if (r != 0) ret = r == 1 ? 1 : 0;
    }
    if (ret != 0) GRef<s32>(save) = 0;
    return ret;
}

// EE: 0x0025F900
// New save: card check, the directory must not exist yet (status 9), mkdir, then every registered
// file (mask +0xAC0) in index order.
s32 CreateMcSave(u32 save, s32 port) {
    s32 ret = 0;
    const u32 block = save + static_cast<u32>(port) * 0x540 + 0x40;
    const s32 step = GRef<s32>(save);
    if (!(static_cast<u32>(step) < 7)) return 0;
    switch (step) {
        case 0:
        case 1:
            if (step == 0) {
                GRef<s32>(block + 0x4A4) = 0;
                GRef<s32>(save) = 1;
                GRef<s32>(save + 0xAC4) = 0;
            }
            ret = CheckSaveCard(save, port, block);
            break;
        case 2: {
            const s32 r = ReadMcDir(block, block + 0x4A8);
            if (r == -1) {
                ret = -1;
            } else if (r == 1) {
                if (GRef<s32>(block + 0x498) != 0) {
                    ret = -1;
                    GRef<s32>(block + 0x4A4) = 9;
                } else {
                    GRef<s32>(save) = 3;
                }
            }
            break;
        }
        case 3: {
            const s32 r = MakeMcDir(block, block + 0x4A8);
            if (r == -1) {
                ret = -1;
            } else if (r == 1) {
                GRef<s32>(save) = 4;
            }
            break;
        }
        case 4: {
            const s32 index = GRef<s32>(save + 0xAC4);
            if (((GRef<s32>(save + 0xAC0) >> (index & 0x1F)) & 1) != 0) {
                GRef<s32>(save) = 5;
            } else {
                GRef<s32>(save + 0xAC4) = index + 1;
                if (!(index + 1 < 6)) GRef<s32>(save) = 6;
            }
            break;
        }
        case 5: {
            const u32 e = save + 0xAC8 + static_cast<u32>(GRef<s32>(save + 0xAC4)) * 0xC;
            const s32 r = WriteMcFile(block, GRef<u32>(e + 4), GRef<u32>(e), GRef<s32>(e + 8));
            if (r == -1) {
                ret = -1;
            } else if (r == 1) {
                GRef<s32>(save) = 4;
                GRef<s32>(save + 0xAC4) = GRef<s32>(save + 0xAC4) + 1;
            }
            break;
        }
        case 6:
            ret = 1;
            break;
        default:
            break;
    }
    if (ret != 0) GRef<s32>(save) = 0;
    return ret;
}

// EE: 0x0025FAF0
// Reads the current registered file (+0xAC4) after the card check.
s32 LoadMcSaveFile(u32 save, s32 port) {
    s32 ret = 0;
    const u32 block = save + static_cast<u32>(port) * 0x540 + 0x40;
    const s32 step = GRef<s32>(save);
    const u32 e = save + 0xAC8 + static_cast<u32>(GRef<s32>(save + 0xAC4)) * 0xC;
    if (step == 0 || step == 1) {
        if (step == 0) {
            GRef<s32>(block + 0x4A4) = 0;
            GRef<s32>(save) = 1;
        }
        ret = CheckSaveCard(save, port, block);
    } else if (step == 2) {
        const s32 r = ReadMcFile(block, GRef<u32>(e + 4), GRef<u32>(e), GRef<s32>(e + 8));
        ret = r == -1 ? -1 : (r == 1 ? 1 : 0);
    }
    if (ret != 0) GRef<s32>(save) = 0;
    return ret;
}

// EE: 0x0025FC20
// Writes the current registered file (+0xAC4) over the existing one after the card check.
s32 OverwriteMcSaveFile(u32 save, s32 port) {
    s32 ret = 0;
    const u32 block = save + static_cast<u32>(port) * 0x540 + 0x40;
    const s32 step = GRef<s32>(save);
    const u32 e = save + 0xAC8 + static_cast<u32>(GRef<s32>(save + 0xAC4)) * 0xC;
    if (step == 0 || step == 1) {
        if (step == 0) {
            GRef<s32>(block + 0x4A4) = 0;
            GRef<s32>(save) = 1;
        }
        ret = CheckSaveCard(save, port, block);
    } else if (step == 2) {
        const s32 r = WriteMcFile(block, GRef<u32>(e + 4), GRef<u32>(e), GRef<s32>(e + 8));
        ret = r == -1 ? -1 : (r == 1 ? 1 : 0);
    }
    if (ret != 0) GRef<s32>(save) = 0;
    return ret;
}

// EE: 0x0025FD50
// Deletes every registered file (a missing file, status -4, is skipped), then the directory.
s32 DeleteMcSave(u32 save, s32 port) {
    guest::GuestFrame frame(0x100);
    s32 ret = 0;
    const u32 block = save + static_cast<u32>(port) * 0x540 + 0x40;
    const s32 step = GRef<s32>(save);
    auto next_file = [save]() {
        for (;;) {
            const s32 i = GRef<s32>(save + 0xAC4) + 1;
            GRef<s32>(save + 0xAC4) = i;
            if (((GRef<s32>(save + 0xAC0) >> (static_cast<u32>(i) & 0x1F)) & 1) != 0) return;
            if (5 < i) {
                GRef<s32>(save) = 3;
                return;
            }
        }
    };
    if (step == 0 || step == 1) {
        if (step == 0) {
            GRef<s32>(block + 0x4A4) = 0;
            GRef<s32>(save) = 1;
            GRef<s32>(save + 0xAC4) = 0;
        }
        const s32 r = CheckMcCard(save, port);
        if (r == -1) {
            ret = -1;
        } else if (r == 1) {
            GRef<s32>(save) = 2;
        }
    } else if (step == 2) {
        const s32 index = GRef<s32>(save + 0xAC4);
        u32 path = frame.Address();
        if (index == 5) {
            const u64 args[2] = {block + 0x4A8, block + 0x4A8};
            ee::sprintf(path, 0x00411700, args, 2);  // "%s%s"
        } else {
            path = GRef<u32>(save + 0xAC8 + static_cast<u32>(index) * 0xC);
        }
        const s32 r = DeleteMcFile(block, path);
        if (r == -1) {
            if (GRef<s32>(block + 0x4A4) == -4) {
                next_file();
            } else {
                ret = -1;
            }
        } else if (r == 1) {
            next_file();
        }
    } else if (step == 3) {
        ret = DeleteMcFile(block, block + 0x4A8);
    }
    if (ret != 0) GRef<s32>(save) = 0;
    return ret;
}

}  // namespace ac04::game
