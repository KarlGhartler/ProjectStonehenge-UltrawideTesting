// libmc (SDK 2.3, libmc 2.3.2) on a PCSX2 folder memory card (docs/formats/folder-memcard.md).
// Only slot 1 (port 0) can hold a card; port 1 is always empty.
//
// Asynchronous model: each request is accepted at once and finishes a fixed number of vblanks
// later; sceMcSync(1, ...) reports it finished (writing the command number, the result and the
// request's output) on the first call at or after that vblank. The delays are calibrated against
// PCSX2 2.8.2 with folder cards (docs/pcsx2/sessions-06.md: scene 6 reaches sub 7 at the same frame
// as on PCSX2 with a save card, an unformatted card and no card, and the game's poll counters
// 0x004118A8 / 0x004118CC match): GetInfo 1 vblank, GetDir 1 vblank (two polls, the first in the
// issue frame). The file requests of slice S2b (open, read, write, flush, close, mkdir, delete,
// format) take 1 vblank each (HYPOTHESIS until calibrated against PCSX2) and act on the folder card
// when they complete; writes reach the host file at flush/close.
#include <algorithm>
#include <array>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

#include "common/guest_ref.h"
#include "platform/log.h"
#include "sce/runtime.h"
#include "sce/sce.h"
#include "sce/sce_host.h"

using ac04::guest::GRef;

namespace {

// libmc command numbers as sceMcSync reports them.
constexpr s32 kFuncGetInfo = 1;
constexpr s32 kFuncOpen = 2;
constexpr s32 kFuncClose = 3;
constexpr s32 kFuncRead = 5;
constexpr s32 kFuncWrite = 6;
constexpr s32 kFuncFlush = 0xA;
constexpr s32 kFuncMkdir = 0xB;
constexpr s32 kFuncGetDir = 0xD;
constexpr s32 kFuncDelete = 0xF;
constexpr s32 kFuncFormat = 0x10;
constexpr u64 kGetInfoDelay = 1;
constexpr u64 kGetDirDelay = 1;
constexpr u64 kFileDelay = 1;  // HYPOTHESIS: open/read/write/flush/close/mkdir/delete/format

// libmc results of the file requests.
constexpr s32 kResultNoEntry = -4;
constexpr s32 kResultDenied = -5;
constexpr s32 kResultTooManyFiles = -7;

// GetInfo results: 0 card OK (same card), -2 unformatted card, -11 no card (sceMcResFailDetect2;
// the game clamps anything below -10 to -11, and PCSX2 run 06 leaves -11).
constexpr s32 kResultOk = 0;
constexpr s32 kResultUnformatted = -2;
constexpr s32 kResultNoCard = -11;

struct Request {
    bool active = false;
    s32 func = 0;
    u64 done_vblank = 0;
    s32 result = 0;
    // GetInfo outputs
    u32 type_addr = 0, free_addr = 0, format_addr = 0;
    s32 type = 0, free = 0, format = 0;
    // GetDir output
    u32 table = 0;
    std::vector<ac04::platform::memcard::McEntry> entries;
    // File requests: card port, path, open mode, descriptor, guest buffer, size, written bytes.
    s32 port = 0;
    std::string path;
    s32 mode = 0;
    s32 fd = -1;
    u32 buf = 0;
    s32 size = 0;
    std::vector<u8> data;
};

Request g_request;

// Open files (libmc allows a few at a time; the game uses one).
struct OpenFile {
    bool used = false;
    s32 port = 0;
    std::string path;
    s32 mode = 0;
    std::vector<u8> data;
    std::size_t pos = 0;
    bool dirty = false;
};
std::array<OpenFile, 8> g_files;

std::int64_t HostNow() { return static_cast<std::int64_t>(std::time(nullptr)); }

u64 Now() { return ac04::sce::GameRuntime::Get().VBlankCount(); }
bool Busy();

ac04::platform::memcard::FolderCard* Card(s32 port) {
    if (port != 0) return nullptr;
    auto* card = ac04::sce::Host().memcard.get();
    return card != nullptr && card->Present() ? card : nullptr;
}

void PutTime(u32 addr, const ac04::platform::memcard::McTime& t) {
    GRef<u8>(addr) = 0;
    GRef<u8>(addr + 1) = t.sec;
    GRef<u8>(addr + 2) = t.min;
    GRef<u8>(addr + 3) = t.hour;
    GRef<u8>(addr + 4) = t.day;
    GRef<u8>(addr + 5) = t.month;
    GRef<u16>(addr + 6) = t.year;
}

void Complete(const Request& r) {
    if (r.func == kFuncGetInfo) {
        if (r.type_addr != 0) GRef<s32>(r.type_addr) = r.type;
        if (r.free_addr != 0) GRef<s32>(r.free_addr) = r.free;
        if (r.format_addr != 0) GRef<s32>(r.format_addr) = r.format;
    } else if (r.func == kFuncGetDir) {
        u32 p = r.table;
        for (const auto& e : r.entries) {
            // sceMcTblGetDir (0x40 bytes): created, modified, size, attributes, reserved,
            // PDA application number, name[32].
            PutTime(p, e.created);
            PutTime(p + 8, e.modified);
            GRef<u32>(p + 0x10) = e.size;
            GRef<u16>(p + 0x14) = e.mode;
            GRef<u16>(p + 0x16) = 0;
            GRef<u32>(p + 0x18) = 0;
            GRef<u32>(p + 0x1C) = 0;
            for (u32 i = 0; i < 32; ++i) GRef<u8>(p + 0x20 + i) = i < e.name.size() ? static_cast<u8>(e.name[i]) : 0;
            p += 0x40;
        }
    }
}

// Writes an open file's data to the card (flush/close of a file written to).
s32 Commit(OpenFile& f) {
    if (!f.dirty) return 0;
    auto* card = Card(f.port);
    if (card == nullptr) return kResultNoCard;
    const int r = card->WriteFile(f.path, f.data, HostNow());
    if (r == 0) f.dirty = false;
    return r;
}

// Runs a file request when it completes and sets its result.
void CompleteFile(Request& r) {
    auto* card = Card(r.port);
    switch (r.func) {
        case kFuncOpen: {
            if (card == nullptr) {
                r.result = kResultNoCard;
                break;
            }
            if (!card->Formatted()) {
                r.result = kResultUnformatted;
                break;
            }
            auto slot = std::find_if(g_files.begin(), g_files.end(), [](const OpenFile& f) { return !f.used; });
            if (slot == g_files.end()) {
                r.result = kResultTooManyFiles;
                break;
            }
            OpenFile f;
            f.port = r.port;
            f.path = r.path;
            f.mode = r.mode;
            if (auto existing = card->ReadFile(r.path)) {
                f.data = std::move(*existing);
            } else if ((r.mode & 0x200) == 0) {
                r.result = kResultNoEntry;
                break;
            } else {
                // Create: the directory must exist; the empty file appears on the card at once.
                const std::size_t slash = r.path.find_last_of('/');
                if (slash == std::string::npos || slash == 0 || !card->DirExists(r.path.substr(0, slash))) {
                    r.result = kResultNoEntry;
                    break;
                }
                f.dirty = true;
            }
            f.used = true;
            *slot = std::move(f);
            r.result = static_cast<s32>(slot - g_files.begin());
            break;
        }
        case kFuncRead:
        case kFuncWrite:
        case kFuncFlush:
        case kFuncClose: {
            if (r.fd < 0 || r.fd >= static_cast<s32>(g_files.size()) || !g_files[r.fd].used) {
                r.result = kResultDenied;
                break;
            }
            OpenFile& f = g_files[r.fd];
            if (r.func == kFuncRead) {
                const std::size_t n = std::min<std::size_t>(static_cast<std::size_t>(std::max(r.size, 0)),
                                                             f.data.size() - std::min(f.pos, f.data.size()));
                for (std::size_t i = 0; i < n; ++i) GRef<u8>(r.buf + static_cast<u32>(i)) = f.data[f.pos + i];
                f.pos += n;
                r.result = static_cast<s32>(n);
            } else if (r.func == kFuncWrite) {
                if ((f.mode & 3) == 1) {
                    r.result = kResultDenied;
                    break;
                }
                if (f.data.size() < f.pos + r.data.size()) f.data.resize(f.pos + r.data.size());
                std::copy(r.data.begin(), r.data.end(), f.data.begin() + static_cast<std::ptrdiff_t>(f.pos));
                f.pos += r.data.size();
                f.dirty = true;
                r.result = static_cast<s32>(r.data.size());
            } else if (r.func == kFuncFlush) {
                r.result = Commit(f);
            } else {
                r.result = Commit(f);
                f = OpenFile{};
            }
            break;
        }
        case kFuncMkdir:
            r.result = card == nullptr ? kResultNoCard : card->MakeDir(r.path, HostNow());
            break;
        case kFuncDelete:
            r.result = card == nullptr ? kResultNoCard : card->Delete(r.path);
            break;
        case kFuncFormat:
            r.result = card == nullptr ? kResultNoCard : card->Format();
            break;
        default:
            break;
    }
}

// Queues a file request (one vblank).
s32 Queue(Request r) {
    if (Busy()) return -1;
    r.active = true;
    r.done_vblank = Now() + kFileDelay;
    g_request = std::move(r);
    return 0;
}

bool Busy() {
    if (!g_request.active) return false;
    ac04::log::WriteOnce("libmc:busy", ac04::log::Level::Warning, "sce",
                         "libmc: request issued while another one is running (rejected)");
    return true;
}

}  // namespace

// PS2: binds the MCSERV RPC; returns 0.
s32 sceMcInit() {
    g_request = Request{};
    g_files = {};
    return 0;
}

// PS2 (libmc 2.3.2): queues "get card info" for port/slot; the results land in *type (0 none,
// 1 PS1, 2 PS2, 3 PocketStation), *free (free clusters of 1 KiB) and *format (1 formatted) when the
// request completes. Returns 0 when accepted (H: a negative value when another request runs).
// sceMcSync's result: 0 same card, -1 new formatted card, -2 unformatted card, <= -10 no card.
s32 sceMcGetInfo(s32 port, s32 slot, u32 type, u32 free, u32 format) {
    (void)slot;
    if (Busy()) return -1;
    Request r;
    r.active = true;
    r.func = kFuncGetInfo;
    r.done_vblank = Now() + kGetInfoDelay;
    r.type_addr = type;
    r.free_addr = free;
    r.format_addr = format;
    auto* card = Card(port);
    if (card == nullptr) {
        r.result = kResultNoCard;
    } else if (!card->Formatted()) {
        r.type = 2;
        r.result = kResultUnformatted;
    } else {
        r.type = 2;
        r.free = card->FreeClusters();
        r.format = 1;
        r.result = kResultOk;
    }
    g_request = std::move(r);
    return 0;
}

// PS2 (libmc 2.3.2): queues a directory listing of `name` (wildcards allowed in the last
// component); up to `maxent` sceMcTblGetDir entries are written to `table`. `mode` 0 starts a new
// listing. sceMcSync's result is the number of entries (0 when nothing matches), or negative on a
// card error. Returns 0 when accepted.
s32 sceMcGetDir(s32 port, s32 slot, u32 name, u32 mode, s32 maxent, u32 table) {
    (void)slot;
    (void)mode;
    if (Busy()) return -1;
    Request r;
    r.active = true;
    r.func = kFuncGetDir;
    r.done_vblank = Now() + kGetDirDelay;
    r.table = table;
    auto* card = Card(port);
    if (card == nullptr) {
        r.result = kResultNoCard;
    } else if (!card->Formatted()) {
        r.result = kResultUnformatted;
    } else {
        r.entries = card->GetDir(ac04::guest::GStr(name, 1024), static_cast<std::size_t>(std::max(maxent, 0)));
        r.result = static_cast<s32>(r.entries.size());
    }
    g_request = std::move(r);
    return 0;
}

// PS2 (libmc 2.3.2): queues opening `name` on port/slot with `mode` (1 read, 2 write, 3 read/write,
// 0x200 create). sceMcSync's result is the file descriptor, or -4 when the file (or, for create,
// its directory) does not exist, -2 unformatted, <= -10 no card. Returns 0 when accepted.
s32 sceMcOpen(s32 port, s32 slot, u32 name, s32 mode) {
    (void)slot;
    Request r;
    r.func = kFuncOpen;
    r.port = port;
    r.path = ac04::guest::GStr(name, 1024);
    r.mode = mode;
    return Queue(std::move(r));
}

// PS2: queues closing `fd` (data written before reaches the card). Result 0.
s32 sceMcClose(s32 fd) {
    Request r;
    r.func = kFuncClose;
    r.fd = fd;
    return Queue(std::move(r));
}

// PS2: queues reading up to `size` bytes of `fd` into guest memory at `buf`; the result is the
// number of bytes read.
s32 sceMcRead(s32 fd, u32 buf, s32 size) {
    Request r;
    r.func = kFuncRead;
    r.fd = fd;
    r.buf = buf;
    r.size = size;
    return Queue(std::move(r));
}

// PS2: queues writing `size` bytes from guest memory at `buf` to `fd` (the data is taken when the
// request is issued); the result is the number of bytes written.
s32 sceMcWrite(s32 fd, u32 buf, s32 size) {
    Request r;
    r.func = kFuncWrite;
    r.fd = fd;
    r.buf = buf;
    r.size = size;
    r.data.resize(static_cast<std::size_t>(std::max(size, 0)));
    for (std::size_t i = 0; i < r.data.size(); ++i) r.data[i] = GRef<u8>(buf + static_cast<u32>(i));
    return Queue(std::move(r));
}

// PS2: queues flushing `fd`'s written data to the card. Result 0 (or a card error).
s32 sceMcFlush(s32 fd) {
    Request r;
    r.func = kFuncFlush;
    r.fd = fd;
    return Queue(std::move(r));
}

// PS2: queues creating directory `name`. Result 0, or -4 (H) when it exists or has no parent.
s32 sceMcMkdir(s32 port, s32 slot, u32 name) {
    (void)slot;
    Request r;
    r.func = kFuncMkdir;
    r.port = port;
    r.path = ac04::guest::GStr(name, 1024);
    return Queue(std::move(r));
}

// PS2: queues deleting file `name` or the empty directory `name`. Result 0, -4 when it does not
// exist, -6 when the directory is not empty.
s32 sceMcDelete(s32 port, s32 slot, u32 name) {
    (void)slot;
    Request r;
    r.func = kFuncDelete;
    r.port = port;
    r.path = ac04::guest::GStr(name, 1024);
    return Queue(std::move(r));
}

// PS2: queues formatting the card on port/slot. Result 0.
s32 sceMcFormat(s32 port, s32 slot) {
    (void)slot;
    Request r;
    r.func = kFuncFormat;
    r.port = port;
    return Queue(std::move(r));
}

// PS2 (libmc 2.3.2): mode 0 waits for the running request, mode 1 only checks. Returns 1 when a
// request finished (the command number goes to *cmd and the result to *result), 0 while it runs,
// -1 when no request is pending.
s32 sceMcSync(s32 mode, u32 cmd, u32 result) {
    if (!g_request.active) return -1;
    if (mode == 1 && Now() < g_request.done_vblank) return 0;
    if (g_request.func == kFuncGetInfo || g_request.func == kFuncGetDir) {
        Complete(g_request);
    } else {
        CompleteFile(g_request);
    }
    g_request.active = false;
    if (cmd != 0) GRef<s32>(cmd) = g_request.func;
    if (result != 0) GRef<s32>(result) = g_request.result;
    return 1;
}

// Reached only from dead game code (UnformatMcCard 0x0025EB58, ChangeMcDir 0x0025F170): log once and
// fail the request like a libmc call that could not be issued.
s32 sceMcUnformat(s32, s32) {
    AC04_SCE_STUB("sceMcUnformat");
    return -1;
}

s32 sceMcChdir(s32, s32, u32, u32) {
    AC04_SCE_STUB("sceMcChdir");
    return -1;
}
