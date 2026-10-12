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

}  // namespace ac04::game
