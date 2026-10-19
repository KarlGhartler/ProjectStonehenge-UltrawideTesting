// CD file pipeline (docs/game/file-loading.md, docs/formats/CDP.md): the file table, two request
// queues (0 = streams into IOP memory, 1 = data into EE memory), the request state machine and the
// DATA.TBL / DATA.CDP access helpers.
//
// g_CdFsState (0x00410C68 -> 0x0029F180), as u32 words:
//   [0] TOC buffer  [1] TOC first-track number  [2] media mode  [5]/[6] last ticket per queue
//   [7] ticket being processed  [8]/[9] done ticket per queue  [10]/[11] -1  [12] 1 = idle
//   [13] 1  [14] break request  [15] request state  [0x10] sceCdRMode (bytes +0x40..+0x42)
//   [0x11] sceCdSync result  [0x12] previous sync result  [0x13] disk-ready result  [0x14] timeout
// Queue descriptors at 0x002BCB08 + q*12: {records, read index, write index}; 64 records of
// 0x1C bytes per queue at 0x002BBD08 + q*0x700: {lsn, dest, flags, hdd, sector offset, sectors,
// ticket}.
#include "game/game.h"

#include "common/guest_stack.h"
#include "sce/sce.h"

namespace ac04::game {

using guest::GRef;

AC04_GUEST_VAR(u32, g_CdFsState, 0x00410C68)
constexpr u32 kCdFileTable = 0x002BCBA8;  // 57 x {char* path; u32 lsn}
constexpr u32 kQueueRecords = 0x002BBD08;
constexpr u32 kQueueDesc = 0x002BCB08;

namespace {

u32& St(u32 word) { return GRef<u32>(g_CdFsState() + 4u * word); }
u32& QueueRead(s32 q) { return GRef<u32>(kQueueDesc + 4 + 12u * static_cast<u32>(q)); }
u32& QueueWrite(s32 q) { return GRef<u32>(kQueueDesc + 8 + 12u * static_cast<u32>(q)); }
u32 Record(s32 q, u32 index) { return kQueueRecords + 0x700u * static_cast<u32>(q) + index * 0x1Cu; }

}  // namespace

// EE: 0x002145C8
// The third argument (0x0029D180) is passed by InitSubsystems but not used.
void InitCdFileSystem(u32 state, u32 toc_buffer, u32 /*unused*/) {
    g_CdFsState() = state;
    GRef<u8>(state + 0x40) = 0;
    GRef<u8>(g_CdFsState() + 0x41) = 0;
    GRef<u8>(g_CdFsState() + 0x42) = 0;
    St(9) = 0x80000000u;
    St(0) = toc_buffer;
    St(11) = 0xFFFFFFFFu;
    St(15) = 0;
    St(12) = 1;
    St(13) = 1;
    St(14) = 0;
    St(10) = 0xFFFFFFFFu;
    St(5) = 0;
    St(6) = 0x80000000u;
    St(8) = 0;
    sceCdInit(0);
    const s32 disk_type = sceCdGetDiskType();
    if (disk_type == 0x12) {
        St(2) = 1;
    } else if (disk_type == 0x14) {
        St(2) = 2;
    }
    sceCdMmode(static_cast<s32>(St(2)));
    for (u32 q = 0; q < 2; ++q) {
        GRef<u32>(kQueueDesc + 12 * q) = kQueueRecords + 0x700 * q;
        GRef<u32>(kQueueDesc + 12 * q + 4) = 0;
        GRef<u32>(kQueueDesc + 12 * q + 8) = 0;
    }
    WaitDiscAndReadToc();
}

// EE: 0x002146C0
u32 WaitDiscAndReadToc() {
    const u32 toc = St(0);
    while (sceCdDiskReady(0) != 2) {
    }
    while (sceCdGetToc(toc) == 0) {
    }
    const u8 bcd = GRef<u8>(toc + 0x11);
    St(1) = static_cast<u32>(bcd >> 4) * 10 + (bcd & 0xF);
    return 1;
}

// EE: 0x00214508
void BuildCdLsnTable() {
    guest::GuestFrame file(0x30);  // sceCdlFILE filled by sceCdSearchFile
    for (s32 i = 0; i < 0x39; ++i) {
        const u32 path = GRef<u32>(kCdFileTable + 8u * static_cast<u32>(i));
        u32 lsn = 0;
        do {
            if (sceCdSearchFile(file.Address(), path) == 0) {
                const u64 args[1] = {path};
                ee::printf(0x00405F38 /* "lsn table: %s Not Found\n" */, args, 1);
                lsn = 0;
            } else {
                lsn = GRef<u32>(file.Address());
            }
        } while (lsn == 0);
        GRef<u32>(kCdFileTable + 8u * static_cast<u32>(i) + 4) = lsn;
    }
}

// EE: 0x00214738
// Dead (no reference): LSN of the file named `name`: the 57-entry file table first (strcmp on the
// paths), else sceCdSearchFile (0 when not found).
u32 FindCdFileLsnByName(u32 name) {
    for (s32 i = 0; i < 0x39; ++i) {
        const u32 e = kCdFileTable + 8u * static_cast<u32>(i);
        if (ee::strcmp(name, GRef<u32>(e)) == 0) return GRef<u32>(e + 4);
    }
    guest::GuestFrame frame(0x30);
    const u32 fp = frame.Address();
    u32 lsn = 0;
    if (sceCdSearchFile(fp, name) != 0) lsn = GRef<u32>(fp);
    return lsn;
}

// EE: 0x002147F0
u32 GetCdFileLsn(s32 file) {
    const u32 hdd = func_002597B8();
    u32 lsn = GRef<u32>(kCdFileTable + 8u * static_cast<u32>(file) + 4);
    // DATA.CDP installed on the HDD (flag never set in this build, docs/subsystems/save.md).
    if (file == 0 && GRef<u8>(hdd + 0xE) != 0) lsn = 0xFFFFFFFFu;
    return lsn;
}

// EE: 0x00214258
u32 PushCdReadRequest(s32 q, u32 lsn, u32 flags, u32 dest, u32 offset, u32 sectors) {
    const u32 write = QueueWrite(q);
    const u32 read = QueueRead(q);
    u32 next = write + 1;
    if (next > 0x3F) next = 0;
    const u32 rec = Record(q, write);
    GRef<u32>(rec + 8) = flags;
    GRef<u32>(rec + 4) = dest;
    GRef<u32>(rec + 16) = offset;
    GRef<u32>(rec + 20) = sectors;
    GRef<u32>(rec + 12) = lsn == 0xFFFFFFFFu ? 1 : 0;  // HDD request
    GRef<u32>(rec + 0) = lsn;
    if (next == read) return 0xFFFFFFFFu;  // queue full
    QueueWrite(q) = next;
    St(static_cast<u32>(q) + 5) += 1;
    if (q == 0) {
        if (static_cast<s32>(St(5)) > 0x7FFFFEFF) St(5) = 0;
    } else if (static_cast<s32>(St(6)) > -0x101) {
        St(6) = 0x80000000u;
    }
    GRef<u32>(rec + 24) = St(static_cast<u32>(q) + 5);
    return St(static_cast<u32>(q) + 5);
}

// EE: 0x00214870
// Splits a read into chunks of `chunk` sectors (chunk < 0: one request). Quirks kept from the
// original (file-loading.md): the sector offset advances by 2 x chunk per request, and the last
// request asks for `count % chunk` sectors (0 when the count is a multiple of the chunk).
u32 QueueCdRead(s32 q, u32 lsn, u64 flags, u32 dest, u32 offset, s32 count, s64 chunk) {
    u32 ticket = 0xFFFFFFFFu;
    s32 requests = 0;
    s32 size = 0;
    const s32 c = static_cast<s32>(chunk);
    if (c < 0) {
        requests = 1;
        size = count;
    } else {
        requests = (count % c != 0 ? 1 : 0) + count / c;
        size = c;
    }
    for (s32 i = 0; i < requests; ++i) {
        if (i == requests - 1 && c > 0) size = count % c;
        ticket = PushCdReadRequest(q, lsn, static_cast<u32>(flags), dest, offset, static_cast<u32>(size));
        offset += static_cast<u32>(size) * 2;
        dest += static_cast<u32>(size) * 0x800;
    }
    return ticket;
}

// EE: 0x00214490
u32 GetCdDoneTicket(s32 q) {
    return St(static_cast<u32>(q) + 8);
}

// EE: 0x00214438
void FlushCdQueue(s32 q) {
    QueueWrite(q) = QueueRead(q);
}

// EE: 0x00214350
s32 UpdateCdQueue() {
    if (St(12) == 1) {
        u32 rec;
        s32 q;
        if (QueueRead(0) == QueueWrite(0)) {
            if (QueueRead(1) == QueueWrite(1)) return -1;
            q = 1;
            rec = Record(1, QueueRead(1));
        } else {
            q = 0;
            rec = Record(0, QueueRead(0));
        }
        St(7) = GRef<u32>(rec + 24);
        return ProcessCdRequest(rec, q);
    }
    if (static_cast<s32>(St(7)) < 0) return ProcessCdRequest(Record(1, QueueRead(1)), 1);
    return ProcessCdRequest(Record(0, QueueRead(0)), 0);
}

// EE: 0x00214990
void PollCdSync(u32 rec) {
    if (GRef<u32>(rec + 12) == 0) St(0x11) = static_cast<u32>(sceCdSync(1));
    if (St(0x12) != St(0x11)) St(0x12) = St(0x11);
}

// EE: 0x002149D0
s32 IssueCdRead(u32 rec) {
    St(0x14) = 0;
    const u32 lsn = GRef<u32>(rec) + GRef<u32>(rec + 16);
    const u32 sectors = GRef<u32>(rec + 20);
    const u32 dest = GRef<u32>(rec + 4);
    s32 r;
    if ((GRef<u32>(rec + 8) & 1) == 0) {
        r = sceCdRead(lsn, sectors, dest, g_CdFsState() + 0x40);
    } else {
        r = sceCdReadIOPm(lsn, sectors, dest, g_CdFsState() + 0x40);  // into IOP memory
    }
    return r == 1 ? 1 : -2;
}

// EE: 0x00214A40
s32 ProcessCdRequest(u32 rec, s32 q) {
    PollCdSync(rec);
    s32 result = 0;
    switch (St(15)) {
        case 0:
            St(12) = 2;
            if (St(0x11) != 0) return 0;
            {
                const s32 ready = sceCdDiskReady(1);
                St(0x13) = static_cast<u32>(ready);
                if (ready == 6) return 0;  // not ready
            }
            St(14) = 0;
            St(15) = 1;
            [[fallthrough]];
        case 1: {
            s32 r;
            if (GRef<u32>(rec + 12) == 0) {
                r = IssueCdRead(rec);
            } else {
                r = 1;  // HDD request: completed by the HDD job (unreachable)
                St(0x11) = 1;
            }
            if (r == 1) {
                St(15) = 2;
            } else if (r == -2) {
                St(15) = 3;
            }
            break;
        }
        case 2: {
            if (St(0x11) == 0) {
                St(static_cast<u32>(q) + 8) = St(7);
                St(12) = 1;
                u32& read = QueueRead(q);
                read += 1;
                if (read > 0x3F) read = 0;
                FlushCache(0);
                St(15) = 0;
                return 1;
            }
            if (GRef<u32>(rec + 12) != 0) {
                // HDD read job (unreachable in this build).
                const s32 r = func_0025ACD0(func_002597B8(), rec);
                if (r == 0) {
                    St(0x11) = 1;
                    return 0;
                }
                if (r < 1) {
                    if (r != -2) return r;
                    func_002597C8();
                    St(15) = 3;
                    St(0x11) = 0;
                    return -2;
                }
                if (r != 1) return r;
                St(0x11) = 0;
                return 1;
            }
            u32 count;
            if (St(14) == 0) {
                count = St(0x14);
            } else {
                if (sceCdBreak() != 0) {
                    St(15) = 4;
                    return 0;
                }
                count = St(0x14);
            }
            St(0x14) = count + 1;
            if (static_cast<s32>(count + 1) < 0xE10) return 0;  // 3600-frame timeout
            if (sceCdBreak() == 0) return 0;
            St(15) = 3;
            break;
        }
        case 3:
            if (St(0x11) == 0) {
                if (GRef<u32>(rec + 12) == 0) {
                    const s32 ready = sceCdDiskReady(1);
                    St(0x13) = static_cast<u32>(ready);
                    if (ready != 6) St(15) = 1;
                } else {
                    GRef<u8>(func_002597B8() + 0xE) = 0;
                    St(12) = 1;
                    St(15) = 0;
                    FlushCdQueue(1);
                    EnterHddErrorScene();
                }
            }
            result = 0;
            break;
        case 4:
            if (St(0x11) == 0) {
                St(static_cast<u32>(q) + 8) = St(7);
                u32& read = QueueRead(q);
                read += 1;
                if (read > 0x3F) read = 0;
                St(15) = 0;
                St(12) = 1;
                result = -2;
            }
            break;
        default:
            break;
    }
    return result;
}

// EE: 0x00215450
u32 GetTblBuffer(s32 tbl) {
    return 0x00421880u + 0x1000u * static_cast<u32>(tbl);
}

// EE: 0x00215468
u32 GetTblEntrySector(s32 tbl, s32 entry) {
    return GRef<u32>(0x00421880u + 8u * static_cast<u32>(entry) + 0x1000u * static_cast<u32>(tbl));
}

// EE: 0x00215488
u32 GetTblEntrySize(s32 tbl, s32 entry) {
    return GRef<u32>(0x00421884u + 8u * static_cast<u32>(entry) + 0x1000u * static_cast<u32>(tbl));
}

// EE: 0x002154A8
// Bytes to allocate for DATA entry n: whole sectors, plus one extra sector (as the original).
u32 GetDataEntryAllocSize(s32 entry) {
    const s32 size = static_cast<s32>(GetTblEntrySize(0, entry));
    const s32 rounded = size < 0 ? size + 0x7FF : size;
    return static_cast<u32>(((rounded >> 11) + 1) * 0x800);
}

// EE: 0x002154E8
void LoadDataEntryAsync(s32 entry, u32 dest) {
    const u32 lsn = GetCdFileLsn(0);
    const s32 size = static_cast<s32>(GetTblEntrySize(0, entry));
    const u32 sector = GetTblEntrySector(0, entry);
    const s32 rounded = size < 0 ? size + 0x7FF : size;
    GRef<u32>(0x004117F4) = QueueCdRead(1, lsn, 0, dest, sector, (rounded >> 11) + 1, -1);
}

// EE: 0x00215588
s32 IsDataLoadDone() {
    const bool done = GRef<u32>(0x004117F4) <= GetCdDoneTicket(1);
    if (done) FlushCache(0);
    return done ? 1 : 0;
}

// EE: 0x00214D80
// Requests an abort of the CD queue (state word 0xE, +0x38). Called only by the HDD install cancel
// 0x0025A068 (dead in this build).
void func_00214D80() { St(0xE) = 1; }

// EE: 0x00214460
// Dead (no callers): 1 when CD queue `q` is empty (read index == write index).
s32 func_00214460(s32 q) { return QueueRead(q) == QueueWrite(q) ? 1 : 0; }

// EE: 0x002144A8
// Dead (only caller 0x002153F8 is dead): state word 0xC.
u32 func_002144A8() { return St(0xC); }

// EE: 0x002144B8
// Dead (no callers): state word 0xB = sceCdStatus().
void func_002144B8() { St(0xB) = static_cast<u32>(sceCdStatus()); }

// EE: 0x002144E0
// Dead (no callers): state word 0xA = sceCdGetError().
void func_002144E0() { St(0xA) = static_cast<u32>(sceCdGetError()); }

// EE: 0x002146B8
void func_002146B8() {}

// EE: 0x00214840
// Dead (no callers): sectors of `bytes` (2048 each, rounded up), at most 0x400.
s32 func_00214840(s32 bytes) {
    s32 n = bytes / 0x800;
    if ((bytes & 0x7FF) != 0) n += 1;
    return n < 0x401 ? n : 0x400;
}

// EE: 0x00214D48
// Dead (no callers): sceCdSeek; on success clears state word 0xD.
s32 func_00214D48(u32 lsn) {
    const s32 r = sceCdSeek(lsn);
    if (r == 1) St(0xD) = 0;
    return r;
}

// EE: 0x00214D90
// Dead (only callers dead): normalises a host path in place: cut at ';', backslashes to slashes.
void func_00214D90(u32 path) {
    guest::GuestFrame frame(0x400);
    const u32 buf = frame.Address();
    ee::strcpy(buf, path);
    const u32 semi = ee::strchr(buf, ';');
    if (semi != 0) GRef<u8>(semi) = 0;
    const s32 n = static_cast<s32>(ee::strlen(buf));
    for (s32 i = 0; i < n; ++i) {
        if (GRef<s8>(buf + static_cast<u32>(i)) == '\\') GRef<u8>(buf + static_cast<u32>(i)) = '/';
    }
    ee::strcpy(path, buf);
}

// EE: 0x00214E18
// Dead (no callers): queues a read in requests of `chunk` sectors (all at once when chunk < 0),
// warning "***** BAD ALIGNMENT 64" for a destination not 64-byte aligned. The destination advances
// by the sector count per request, as the original. chunk 0 is a trap (break 7) in the original.
u32 func_00214E18(s32 q, u32 lsn, u32 flags, u32 dest, u32 offset, s32 sectors, s32 chunk) {
    u32 r = 0;
    if ((dest & 0x3F) != 0) {
        const u64 a[1] = {dest};
        ee::printf(0x00406868, a, 1);
    }
    s32 n;
    if (chunk < 0) {
        chunk = sectors;
        n = 1;
    } else {
        n = sectors / chunk;
    }
    for (; 0 < n; --n) {
        r = PushCdReadRequest(q, lsn, flags, dest, offset, static_cast<u32>(chunk));
        offset += static_cast<u32>(chunk);
        dest += static_cast<u32>(chunk);
    }
    return r;
}

// EE: 0x00214F00
// Dead (no callers): index of `path` in the path table 0x002BD1D8 (0x400 bytes per entry, count
// 0x00410C70); a new path is added after the last entry.
s32 func_00214F00(u32 path) {
    for (s32 i = 0; !(GRef<s32>(0x00410C70) < i); ++i) {
        if (ee::strcmp(path, 0x002BD1D8 + static_cast<u32>(i) * 0x400) == 0) return i;
    }
    const s32 n = GRef<s32>(0x00410C70) + 1;
    GRef<s32>(0x00410C70) = n;
    ee::strcpy(0x002BD1D8 + static_cast<u32>(n) * 0x400, path);
    return GRef<s32>(0x00410C70);
}

// EE: 0x00214FB0
// Dead (no reference): as func_00214F00 for the path of file-table entry `file` (0x002BD010 + 8 x
// file) with the entry count at 0x00410C74.
s32 func_00214FB0(s32 file) {
    const u32 path = GRef<u32>(0x002BD010 + 8u * static_cast<u32>(file));
    for (s32 i = 0; !(GRef<s32>(0x00410C74) < i); ++i) {
        if (ee::strcmp(path, 0x002BD1D8 + static_cast<u32>(i) * 0x400) == 0) return i;
    }
    const s32 n = GRef<s32>(0x00410C74) + 1;
    GRef<s32>(0x00410C74) = n;
    ee::strcpy(0x002BD1D8 + static_cast<u32>(n) * 0x400, path);
    return GRef<s32>(0x00410C74);
}

// EE: 0x00215070
// Dead (no reference): host-file read request `rec` (+0 path table index, +4 destination, +8 flags:
// bit 0 through the EE buffer 0x07800000 and SIF DMA, +0x10 offset and +0x14 size in sectors),
// one step per call (state 0x00410C78, file descriptor 0x00410C7C, St(0x11) busy): open
// "host0:" + path (O_RDONLY | O_NOWAIT), seek, read when the async I/O is idle (sceIoctl 1),
// close, then for bit 0 the SIF DMA to the destination (id 0x004117F0); at the end the request is
// marked done like func_00215320. Returns 1 when done, -2 on an error, else 0. The host file calls
// are stubs in the port.
s32 func_00215070(u32 rec) {
    guest::GuestFrame frame(0x420);
    const u32 path = frame.Address(), dma = path + 0x400, status = path + 0x410;
    s32& state = GRef<s32>(0x00410C78);
    s32& fd = GRef<s32>(0x00410C7C);
    s32 r = -1;
    switch (state) {
        case 0: {
            GRef<u32>(path) = GRef<u32>(0x00410C80);  // "host0:"
            GRef<u16>(path + 4) = GRef<u16>(0x00410C84);
            GRef<u8>(path + 6) = GRef<u8>(0x00410C86);
            ee::strcat(path, 0x002BD1D8 + GRef<u32>(rec) * 0x400);
            func_00214D90(path);
            fd = sceOpen(path, 0x8001);
            St(0x11) = 0;
            if (fd < 0) {
                state = -2;
                return -2;
            }
            St(0x11) = 1;
            state = 1;
        }
            [[fallthrough]];
        case 1:
            r = sceLseek(fd, static_cast<s32>(GRef<u32>(rec + 0x10) << 11), 0);
            if (r == -1) break;
            state = 2;
            [[fallthrough]];
        case 2: {
            const s32 size = static_cast<s32>(GRef<u32>(rec + 0x14) << 11);
            sceIoctl(fd, 1, status);
            if (GRef<u32>(status) != 0) return 0;
            const u32 dest = (GRef<u32>(rec + 8) & 1) != 0 ? 0x07800000u : GRef<u32>(rec + 4);
            r = sceRead(fd, dest, size);
            if (r == -1) break;
            state = 3;
        }
            [[fallthrough]];
        case 3:
            sceIoctl(fd, 1, status);
            if (GRef<u32>(status) != 0) return 0;
            r = sceClose(fd);
            if (r == -1) {
                state = 0;
                St(0x11) = 0;
                fd = r;
                return -2;
            }
            state = 4;
            [[fallthrough]];
        case 4:
            fd = -1;
            if ((GRef<u32>(rec + 8) & 1) != 0) {
                GRef<u32>(dma) = 0x07800000;
                GRef<u32>(dma + 4) = GRef<u32>(rec + 4);
                GRef<u32>(dma + 8) = GRef<u32>(rec + 0x14) << 11;
                GRef<u32>(dma + 0xC) = 0;
                const u32 id = sceSifSetDma(dma, 1);
                GRef<u32>(0x004117F0) = id;
                if (id == 0) {
                    St(0x11) = 0;
                    state = 0;
                } else {
                    state = 5;
                }
            } else {
                break;  // done
            }
            [[fallthrough]];
        case 5:
            if (sceSifDmaStat(GRef<u32>(0x004117F0)) >= 0) return 0;
            break;  // done
        default:
            return 0;
    }
    if (state == 1 || state == 2) {
        // Seek or read failed: close and start over.
        sceClose(fd);
        fd = r;
        St(0x11) = 0;
        state = 0;
        return -2;
    }
    const u32 ticket = St(7);
    const u32 q = ticket >> 31;
    St(8 + q) = ticket;
    St(0xC) = 1;
    u32& read = QueueRead(static_cast<s32>(q));
    read += 1;
    if (!(static_cast<s32>(read) < 0x40)) read = 0;
    St(0x11) = 0;
    state = 0;
    return 1;
}

// EE: 0x00215320
// Dead (no callers): loads host file "host0:" + path table entry rec+0 into the IOP heap at rec+4;
// on success marks the request done (done ticket of its queue, idle) and advances the queue.
s32 func_00215320(u32 rec) {
    guest::GuestFrame frame(0x400);
    const u32 path = frame.Address();
    GRef<u32>(path) = GRef<u32>(0x00410C80);  // "host0:"
    GRef<u16>(path + 4) = GRef<u16>(0x00410C84);
    GRef<u8>(path + 6) = GRef<u8>(0x00410C86);
    ee::strcat(path, 0x002BD1D8 + GRef<u32>(rec) * 0x400);
    func_00214D90(path);
    if (sceSifLoadIopHeap(path, GRef<u32>(rec + 4)) < 0) return -2;
    const u32 ticket = St(7);
    const u32 q = ticket >> 31;
    St(8 + q) = ticket;
    St(0xC) = 1;
    u32& read = QueueRead(static_cast<s32>(q));
    read += 1;
    if (!(static_cast<s32>(read) < 0x40)) read = 0;
    return 1;
}

// EE: 0x002153F8
// Dead (no callers): 1 when the CD state is 2 and the processed ticket's queue (bit 31) is `q`, else 2.
s32 func_002153F8(u32 q) {
    if (func_002144A8() != 2) return 2;
    return ((St(7) >> 31) ^ q) == 0 ? 1 : 2;
}

}  // namespace ac04::game
