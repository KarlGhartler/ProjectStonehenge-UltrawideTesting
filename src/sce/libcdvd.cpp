// libcdvd over the user's disc image (ISO 9660, 2048-byte sectors).
// Reads are done when issued; the drive reports "busy" until the next sceCdSync(1) poll, so the
// game's request state machine sees one in-flight poll per read as on hardware (completion timing
// is otherwise not modelled).
#include <cstring>
#include <string>
#include <vector>

#include "common/guest_memory.h"
#include "common/guest_ref.h"
#include "platform/log.h"
#include "sce/sce.h"
#include "sce/sce_host.h"

using ac04::guest::GRef;

namespace {

bool g_busy = false;

bool ReadIso(u32 lsn, u32 sectors, std::vector<u8>& out) {
    const auto* iso = ac04::sce::Host().iso;
    out.assign(static_cast<size_t>(sectors) * 2048u, 0);
    if (iso == nullptr || !iso->ReadSectors(lsn, sectors, out.data())) {
        AC04_LOG_ERROR("cdvd", "read of %u sectors at LSN %u failed", sectors, lsn);
        return false;
    }
    return true;
}

}  // namespace

// PS2: mode 0 initialises the drive (waits for it), mode 5 shuts libcdvd down. Returns 1.
s32 sceCdInit(s32) { return 1; }

// PS2: 0x12 = PS2 CD, 0x14 = PS2 DVD. The supported image is a single-layer DVD.
s32 sceCdGetDiskType() { return 0x14; }

// PS2: sets the media mode (1 CD, 2 DVD); returns 1.
s32 sceCdMmode(s32) { return 1; }

// PS2: 2 = ready (SCECdComplete), 6 = not ready (mode 1, non-blocking).
s32 sceCdDiskReady(s32) { return 2; }

// PS2: reads the disc TOC into `toc`; returns 1. For a DVD the IOP copies 0x810 bytes.
// Port: the bytes PCSX2 2.8.2 returns for a single-layer DVD, checked against scenestep_first.p2s
// (docs/port/validation.md E-1): 04 02 F2 00 86 72 at +0; the DVD physical-format bytes
// 01 02 01 00 at +0x0C (book type, disc size, 1 layer, density); the start PSN 0x00030000 and the
// end PSN (0x30000 + sectors - 1) big-endian at +0x10 and +0x14. The last 16 bytes (+0x800) are IOP
// memory leftovers on the PS2 and stay zero here; WaitDiscAndReadToc only uses byte 0x11.
s32 sceCdGetToc(u32 toc) {
    ac04::guest::Mem().Fill(toc, 0, 0x810);
    const u8 head[6] = {0x04, 0x02, 0xF2, 0x00, 0x86, 0x72};
    ac04::guest::Mem().WriteBytes(toc, head, sizeof(head));
    const u8 format[4] = {0x01, 0x02, 0x01, 0x00};
    ac04::guest::Mem().WriteBytes(toc + 0x0C, format, sizeof(format));
    const auto* iso = ac04::sce::Host().iso;
    const u32 sectors = iso && iso->Source() ? static_cast<u32>(iso->Source()->SizeBytes() / ac04::platform::kSectorSize) : 0;
    const u32 start = 0x00030000u;
    const u32 end = start + sectors - 1;
    const u8 psn[8] = {0, 0x03, 0, 0, static_cast<u8>(end >> 24), static_cast<u8>(end >> 16),
                       static_cast<u8>(end >> 8), static_cast<u8>(end)};
    ac04::guest::Mem().WriteBytes(toc + 0x10, psn, sizeof(psn));
    return 1;
}

// PS2: looks up `name` ("\\BIN\\DATA.CDP;1") and fills sceCdlFILE {u32 lsn, u32 size,
// char name[16], u8 date[8]}; returns 1 when found, 0 otherwise. Names match exactly (ADR-005).
// The date bytes are left zero (expected RAM difference).
s32 sceCdSearchFile(u32 fp, u32 name) {
    const auto* iso = ac04::sce::Host().iso;
    const std::string path = ac04::guest::GStr(name);
    ac04::platform::IsoEntry entry;
    if (iso == nullptr || !iso->Find(path, entry) || entry.is_directory) {
        AC04_LOG_WARN("cdvd", "sceCdSearchFile: %s not found", path.c_str());
        return 0;
    }
    GRef<u32>(fp) = entry.lsn;
    GRef<u32>(fp + 4) = entry.size;
    std::string base = path.substr(path.find_last_of("\\/") == std::string::npos ? 0 : path.find_last_of("\\/") + 1);
    char buf[16] = {};
    std::strncpy(buf, base.c_str(), sizeof(buf) - 1);
    ac04::guest::Mem().WriteBytes(fp + 8, buf, sizeof(buf));
    ac04::guest::Mem().Fill(fp + 0x18, 0, 8);
    return 1;
}

// PS2: starts reading `sectors` sectors at `lsn` into EE memory `buf`; returns 1 if the command
// was accepted.
s32 sceCdRead(u32 lsn, u32 sectors, u32 buf, u32 /*mode*/) {
    std::vector<u8> data;
    if (!ReadIso(lsn, sectors, data)) return 0;
    ac04::guest::Mem().WriteBytes(buf, data.data(), static_cast<u32>(data.size()));
    g_busy = true;
    return 1;
}

// PS2 (hypothesis name): same as sceCdRead, into IOP memory (stream rings). Port: IOP RAM model.
s32 sceCdReadIOPm(u32 lsn, u32 sectors, u32 buf, u32 /*mode*/) {
    std::vector<u8> data;
    if (!ReadIso(lsn, sectors, data)) return 0;
    auto& iop = ac04::sce::IopRam();
    const u64 end = static_cast<u64>(buf & 0x1FFFFFu) + data.size();
    if (end > iop.size()) {
        AC04_LOG_ERROR("cdvd", "IOP read past 2 MiB (0x%08X + 0x%zX)", buf, data.size());
        return 0;
    }
    std::memcpy(iop.data() + (buf & 0x1FFFFFu), data.data(), data.size());
    g_busy = true;
    return 1;
}

// PS2: mode 0 waits for the command to finish; mode 1 returns 1 while busy, 0 when done.
s32 sceCdSync(s32 mode) {
    if (mode == 0) {
        g_busy = false;
        return 0;
    }
    if (g_busy) {
        g_busy = false;  // done at the next poll
        return 1;
    }
    return 0;
}

// PS2: aborts the current command; returns 1.
s32 sceCdBreak() {
    g_busy = false;
    return 1;
}

// PS2: seeks to `lsn`; returns 1.
s32 sceCdSeek(u32) { return 1; }

// HDD power-off callback (dead path).
s32 sceCdPOffCallback(u32, u32) {
    AC04_SCE_STUB("sceCdPOffCallback");
    return 0;
}
s32 sceCdPowerOff(u32) {
    AC04_SCE_STUB("sceCdPowerOff");
    return 0;
}
