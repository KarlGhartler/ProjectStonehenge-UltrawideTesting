#include "platform/render/gs/gs_state.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

#include "platform/log.h"

namespace ac04::render::gs {
namespace {

enum Reg : std::uint32_t {
    kPrim = 0x00, kRgbaq = 0x01, kSt = 0x02, kUv = 0x03, kXyzf2 = 0x04, kXyz2 = 0x05,
    kTex0_1 = 0x06, kTex0_2 = 0x07, kClamp1 = 0x08, kClamp2 = 0x09, kFog = 0x0A,
    kXyzf3 = 0x0C, kXyz3 = 0x0D, kTex1_1 = 0x14, kTex1_2 = 0x15, kTex2_1 = 0x16, kTex2_2 = 0x17,
    kXyoffset1 = 0x18, kXyoffset2 = 0x19, kPrmodecont = 0x1A, kPrmode = 0x1B, kTexclut = 0x1C,
    kTexa = 0x3B, kFogcol = 0x3D, kTexflush = 0x3F, kScissor1 = 0x40, kScissor2 = 0x41,
    kAlpha1 = 0x42, kAlpha2 = 0x43, kDthe = 0x45, kColclamp = 0x46, kTest1 = 0x47, kTest2 = 0x48,
    kPabe = 0x49, kFba1 = 0x4A, kFba2 = 0x4B, kFrame1 = 0x4C, kFrame2 = 0x4D, kZbuf1 = 0x4E, kZbuf2 = 0x4F,
    kBitbltbuf = 0x50, kTrxpos = 0x51, kTrxreg = 0x52, kTrxdir = 0x53,
};

std::uint64_t Bits(std::uint64_t v, int lo, int n) { return (v >> lo) & ((1ull << n) - 1); }

float AsFloat(std::uint32_t bits) {
    float f;
    std::memcpy(&f, &bits, 4);
    return f;
}

std::uint64_t Fnv(const std::uint32_t* p, std::size_t n) {
    std::uint64_t h = 1469598103934665603ull;
    for (std::size_t i = 0; i < n; ++i) {
        h ^= p[i];
        h *= 1099511628211ull;
    }
    return h;
}

bool SameState(const DrawCommand& a, const DrawCommand& b) {
    return a.kind == b.kind && a.gouraud == b.gouraud && a.textured == b.textured && a.fog == b.fog &&
           a.blend == b.blend && a.aa1 == b.aa1 && a.fst == b.fst && a.tex0 == b.tex0 && a.tex1 == b.tex1 &&
           a.clamp == b.clamp && a.alpha == b.alpha && a.test == b.test && a.frame == b.frame &&
           a.zbuf == b.zbuf && a.scissor == b.scissor && a.texa == b.texa && a.fba == b.fba &&
           a.fogcol == b.fogcol && a.colclamp == b.colclamp && a.pabe == b.pabe && a.dthe == b.dthe &&
           a.texture == b.texture && a.texture_from_frame == b.texture_from_frame &&
           a.texture_guest_source == b.texture_guest_source && a.texture_guest_width == b.texture_guest_width;
}

}  // namespace

GsState::GsState() { regs_[kPrmodecont] = 1; }

void GsState::LogOnce(const char* key, const char* fmt, std::uint64_t a, std::uint64_t b) {
    const std::string k = std::string("gs:") + key;
    log::WriteOnce(k.c_str(), log::Level::Warning, "renderer", fmt, static_cast<unsigned long long>(a),
                   static_cast<unsigned long long>(b));
}

std::vector<DrawCommand> GsState::TakeDraws() {
    state_dirty_ = true;
    std::vector<DrawCommand> out;
    out.swap(draws_);
    return out;
}

void GsState::ProcessGif(const std::uint8_t* data, std::size_t bytes, const std::vector<SourceMark>* marks) {
    gif_base_ = data;
    marks_ = marks != nullptr && !marks->empty() ? marks : nullptr;
    struct Reset {
        GsState* s;
        ~Reset() {
            s->gif_base_ = nullptr;
            s->marks_ = nullptr;
        }
    } reset{this};
    std::size_t pos = 0;
    while (pos + 16 <= bytes) {
        std::uint64_t lo, hi;
        std::memcpy(&lo, data + pos, 8);
        std::memcpy(&hi, data + pos + 8, 8);
        pos += 16;
        const std::uint32_t nloop = static_cast<std::uint32_t>(Bits(lo, 0, 15));
        const bool pre = Bits(lo, 46, 1) != 0;
        const std::uint32_t prim = static_cast<std::uint32_t>(Bits(lo, 47, 11));
        const std::uint32_t flg = static_cast<std::uint32_t>(Bits(lo, 58, 2));
        std::uint32_t nreg = static_cast<std::uint32_t>(Bits(lo, 60, 4));
        if (nreg == 0) nreg = 16;
        q_ = 1.0f;
        if (flg == 0) {
            if (pre) WriteRegister(kPrim, prim);
            for (std::uint32_t i = 0; i < nloop; ++i) {
                for (std::uint32_t r = 0; r < nreg; ++r) {
                    if (pos + 16 > bytes) return;
                    std::uint64_t dlo, dhi;
                    std::memcpy(&dlo, data + pos, 8);
                    std::memcpy(&dhi, data + pos + 8, 8);
                    pos += 16;
                    const std::uint32_t desc = static_cast<std::uint32_t>(Bits(hi, static_cast<int>(r * 4), 4));
                    switch (desc) {
                        case 0x0: WriteRegister(kPrim, dlo & 0x7FF); break;
                        case 0x1:
                            cur_.r = static_cast<std::uint8_t>(dlo);
                            cur_.g = static_cast<std::uint8_t>(dlo >> 32);
                            cur_.b = static_cast<std::uint8_t>(dhi);
                            cur_.a = static_cast<std::uint8_t>(dhi >> 32);
                            cur_.q = q_;
                            break;
                        case 0x2:
                            cur_.s = AsFloat(static_cast<std::uint32_t>(dlo));
                            cur_.t = AsFloat(static_cast<std::uint32_t>(dlo >> 32));
                            q_ = AsFloat(static_cast<std::uint32_t>(dhi));
                            break;
                        case 0x3:
                            WriteRegister(kUv, (dlo & 0x3FFF) | ((dlo >> 32) & 0x3FFF) << 16);
                            break;
                        case 0x4: {
                            const std::uint64_t v = (dlo & 0xFFFF) | ((dlo >> 32) & 0xFFFF) << 16 |
                                                    ((dhi >> 4) & 0xFFFFFF) << 32 | ((dhi >> 36) & 0xFF) << 56;
                            WriteRegister(Bits(dhi, 47, 1) ? kXyzf3 : kXyzf2, v);
                            break;
                        }
                        case 0x5: {
                            const std::uint64_t v = (dlo & 0xFFFF) | ((dlo >> 32) & 0xFFFF) << 16 |
                                                    (dhi & 0xFFFFFFFF) << 32;
                            WriteRegister(Bits(dhi, 47, 1) ? kXyz3 : kXyz2, v);
                            break;
                        }
                        case 0xA: WriteRegister(kFog, ((dhi >> 36) & 0xFF) << 56); break;
                        case 0xC: {
                            const std::uint64_t v = (dlo & 0xFFFF) | ((dlo >> 32) & 0xFFFF) << 16 |
                                                    ((dhi >> 4) & 0xFFFFFF) << 32 | ((dhi >> 36) & 0xFF) << 56;
                            WriteRegister(kXyzf3, v);
                            break;
                        }
                        case 0xD: {
                            const std::uint64_t v = (dlo & 0xFFFF) | ((dlo >> 32) & 0xFFFF) << 16 |
                                                    (dhi & 0xFFFFFFFF) << 32;
                            WriteRegister(kXyz3, v);
                            break;
                        }
                        case 0xE: WriteRegister(static_cast<std::uint32_t>(dhi & 0x7F), dlo); break;
                        case 0xF: break;
                        default: WriteRegister(desc, dlo); break;
                    }
                }
            }
        } else if (flg == 1) {
            const std::uint64_t count = static_cast<std::uint64_t>(nloop) * nreg;
            std::uint32_t descs[16];
            for (std::uint32_t r = 0; r < nreg; ++r) descs[r] = static_cast<std::uint32_t>(Bits(hi, static_cast<int>(r * 4), 4));
            for (std::uint32_t i = 0; i < nloop; ++i) {
                for (std::uint32_t r = 0; r < nreg; ++r) {
                    if (pos + 8 > bytes) return;
                    std::uint64_t v;
                    std::memcpy(&v, data + pos, 8);
                    pos += 8;
                    const std::uint32_t desc = descs[r];
                    if (desc == 0xE || desc == 0xF) continue;  // A+D and NOP are not valid in REGLIST
                    WriteRegister(desc, v);
                }
            }
            if ((count & 1) != 0) pos += 8;
        } else {
            const std::size_t n = std::min<std::size_t>(static_cast<std::size_t>(nloop) * 16, bytes - pos);
            ImageData(data + pos, static_cast<std::uint32_t>(n));
            pos += n;
        }
    }
}

void GsState::ImageData(const std::uint8_t* data, std::uint32_t bytes) {
    state_dirty_ = true;
    if (!transfer_active_) {
        LogOnce("image-inactive", "IMAGE data without a host->local transfer (%llu bytes, ignored)", bytes);
        return;
    }
    const std::uint64_t bb = regs_[kBitbltbuf], tp = regs_[kTrxpos], tr = regs_[kTrxreg];
    const std::uint32_t dbp = static_cast<std::uint32_t>(Bits(bb, 32, 14));
    const std::uint32_t dbw = static_cast<std::uint32_t>(Bits(bb, 48, 6));
    const std::uint32_t dpsm = static_cast<std::uint32_t>(Bits(bb, 56, 6));
    const std::uint32_t dsax = static_cast<std::uint32_t>(Bits(tp, 32, 11));
    const std::uint32_t dsay = static_cast<std::uint32_t>(Bits(tp, 48, 11));
    const std::uint32_t rrw = static_cast<std::uint32_t>(Bits(tr, 0, 12));
    const std::uint32_t rrh = static_cast<std::uint32_t>(Bits(tr, 32, 12));
    const int bpp = BitsPerPixel(dpsm);
    if (bpp == 0) {
        LogOnce("image-psm", "IMAGE transfer to unsupported PSM 0x%llx", dpsm);
        return;
    }
    if (transfer_pos_ == 0 && transfer_rest_n_ == 0) {
        // Start of an upload: remember its guest source when the data is marked (movie side path).
        upload_source_.erase(dbp);
        if (marks_ != nullptr && gif_base_ != nullptr) {
            const std::uint32_t off = static_cast<std::uint32_t>(data - gif_base_);
            for (const SourceMark& m : *marks_) {
                if (off >= m.offset && off - m.offset < m.bytes) {
                    upload_source_[dbp] = {m.guest_addr + (off - m.offset), rrw};
                    break;
                }
            }
        }
    }
    // The GS consumes the transfer as one byte stream: a 24-bit pixel can start in one packet and end in
    // the next. Complete a split pixel first, then keep the bytes of a new split pixel for the next packet.
    if (transfer_rest_n_ != 0) {
        const std::uint32_t need = static_cast<std::uint32_t>(bpp) / 8 - transfer_rest_n_;
        const std::uint32_t n = std::min(need, bytes);
        std::copy(data, data + n, transfer_rest_.begin() + transfer_rest_n_);
        transfer_rest_n_ += n;
        data += n;
        bytes -= n;
        if (n < need) return;
        transfer_pos_ = mem_.WriteImage(dbp, dbw, dpsm, dsax, dsay, rrw, rrh, transfer_pos_, transfer_rest_.data(),
                                        transfer_rest_n_);
        transfer_rest_n_ = 0;
    }
    if (bpp >= 8) {
        const std::uint32_t size = static_cast<std::uint32_t>(bpp) / 8;
        transfer_rest_n_ = bytes % size;
        std::copy(data + bytes - transfer_rest_n_, data + bytes, transfer_rest_.begin());
        bytes -= transfer_rest_n_;
    }
    transfer_pos_ = mem_.WriteImage(dbp, dbw, dpsm, dsax, dsay, rrw, rrh, transfer_pos_, data, bytes);
    if (transfer_pos_ >= rrw * rrh) transfer_active_ = false;
}

void GsState::LoadClut(std::uint64_t tex) {
    const std::uint32_t cld = static_cast<std::uint32_t>(Bits(tex, 61, 3));
    const std::uint32_t cbp = static_cast<std::uint32_t>(Bits(tex, 37, 14));
    bool load = false;
    switch (cld) {
        case 1: load = true; break;
        case 2: load = true; cbp_[0] = cbp; break;
        case 3: load = true; cbp_[1] = cbp; break;
        case 4: load = cbp != cbp_[0]; cbp_[0] = cbp; break;
        case 5: load = cbp != cbp_[1]; cbp_[1] = cbp; break;
        default: break;
    }
    if (!load) return;
    const std::uint32_t psm = static_cast<std::uint32_t>(Bits(tex, 20, 6));
    const std::uint32_t cpsm = static_cast<std::uint32_t>(Bits(tex, 51, 4));
    const std::uint32_t csm = static_cast<std::uint32_t>(Bits(tex, 55, 1));
    const std::uint32_t csa = static_cast<std::uint32_t>(Bits(tex, 56, 5));
    if (csm != 0) {
        ++clut_gen_;
        clut_src_.fill(ClutSource{});
        LogOnce("clut-csm2", "CLUT storage mode CSM2 not implemented");
        return;
    }
    const std::uint32_t mem_psm = cpsm == 0 ? kPsmCt32 : (cpsm == 0xA ? kPsmCt16S : kPsmCt16);
    // The model programs request the same palette with every strip: when the part of the CLUT
    // buffer that this load fills already holds that palette (same source, source pages unchanged
    // since), the load would write the same entries, so it is skipped.
    const bool eight = psm == kPsmT8 || psm == kPsmT8H;
    const bool four = psm == kPsmT4 || psm == kPsmT4HL || psm == kPsmT4HH;
    if (!eight && !four) return;
    // The source is contiguous in local memory: a 16x16 CT32 image is blocks cbp..cbp+3 (1 KiB), a
    // 16x16 CT16 one blocks cbp..cbp+1, and the 8x2 image of a 4-bit palette lies in the first 64
    // bytes of block cbp.
    const std::uint32_t slot = eight ? 32 : ((cpsm == 0 ? (csa & 15) : csa) & 31);
    const std::uint32_t addr = cbp * 256u;
    const std::uint32_t len = eight ? (mem_psm == kPsmCt32 ? 1024u : 512u) : 64u;
    const bool comparable = addr + len <= GsMemory::kSize;
    ClutSource& rec = clut_src_[slot];
    if (comparable && rec.valid && rec.cbp == cbp && rec.psm == mem_psm && rec.len == len &&
        std::memcmp(rec.raw.data(), mem_.Data() + addr, len) == 0) {
        return;
    }
    ++clut_gen_;
    if (eight) {
        for (ClutSource& c : clut_src_) c.valid = false;
    } else {
        clut_src_[32].valid = false;  // entries [slot * 16, +16) of the 8-bit palette are replaced
    }
    rec.valid = comparable;
    rec.cbp = cbp;
    rec.psm = mem_psm;
    rec.len = len;
    if (comparable) std::memcpy(rec.raw.data(), mem_.Data() + addr, len);
    if (eight) {
        const std::uint32_t args = 0x1000u | cpsm << 16;  // DecodeTexture's palette hash arguments
        if (comparable) {
            for (ClutCacheEntry& c : clut_cache_) {
                if (c.src.valid && c.src.cbp == cbp && c.src.psm == mem_psm && c.src.len == len &&
                    std::memcmp(c.src.raw.data(), rec.raw.data(), len) == 0) {
                    std::copy(c.entries.begin(), c.entries.end(), clut_.begin());
                    c.used = ++clut_cache_clock_;
                    clut_hash_ = c.hash;
                    clut_hash_gen_ = clut_gen_;
                    clut_hash_args_ = args;
                    return;
                }
            }
        }
        for (std::uint32_t e = 0; e < 256; ++e) {
            // CSM1: bits 3 and 4 of the index are swapped in the stored 16x16 image.
            const std::uint32_t p = (e & 0xE7) | (e & 0x08) << 1 | (e & 0x10) >> 1;
            clut_[e] = mem_.ReadPixel(mem_psm, cbp, 1, p & 15, p >> 4);
        }
        if (comparable) {
            ClutCacheEntry* victim = &clut_cache_[0];
            for (ClutCacheEntry& c : clut_cache_) {
                if (c.used < victim->used) victim = &c;
            }
            victim->src = rec;
            std::copy(clut_.begin(), clut_.begin() + 256, victim->entries.begin());
            victim->hash = Fnv(clut_.data(), 256) ^ cpsm;
            victim->used = ++clut_cache_clock_;
            clut_hash_ = victim->hash;
            clut_hash_gen_ = clut_gen_;
            clut_hash_args_ = args;
        }
    } else {
        const std::uint32_t base = (cpsm == 0 ? (csa & 15) : csa) * 16;
        for (std::uint32_t e = 0; e < 16; ++e) clut_[(base + e) & 511] = mem_.ReadPixel(mem_psm, cbp, 1, e & 7, e >> 3);
    }
}

void GsState::WriteRegister(std::uint32_t reg, std::uint64_t value) {
    reg &= 0x7F;
    // A register that can feed the draw state marks it dirty only when its value changes (the model
    // programs rewrite PRIM and the same A+D registers with every strip). TRXDIR always does: a
    // local->local copy changes texture memory. CLUT loads read local memory, which only IMAGE data
    // and TRXDIR change (both mark the state dirty), so an unchanged TEX0 loads the same palette.
    if (reg != kRgbaq && reg != kSt && reg != kUv && reg != kFog && reg != kXyzf2 && reg != kXyz2 &&
        reg != kXyzf3 && reg != kXyz3) {
        const std::uint64_t stored = reg == kPrim ? (value & 0x7FF) : value;
        if (reg == kTrxdir || regs_[reg] != stored) state_dirty_ = true;
    }
    switch (reg) {
        case kPrim:
            regs_[kPrim] = value & 0x7FF;
            queued_ = 0;
            strip_count_ = 0;
            return;
        case kRgbaq:
            cur_.r = static_cast<std::uint8_t>(value);
            cur_.g = static_cast<std::uint8_t>(value >> 8);
            cur_.b = static_cast<std::uint8_t>(value >> 16);
            cur_.a = static_cast<std::uint8_t>(value >> 24);
            cur_.q = AsFloat(static_cast<std::uint32_t>(value >> 32));
            break;
        case kSt:
            cur_.s = AsFloat(static_cast<std::uint32_t>(value));
            cur_.t = AsFloat(static_cast<std::uint32_t>(value >> 32));
            break;
        case kUv:
            cur_.u = static_cast<float>(value & 0x3FFF) / 16.0f;
            cur_.v = static_cast<float>((value >> 16) & 0x3FFF) / 16.0f;
            break;
        case kFog: cur_.fog = static_cast<std::uint8_t>(value >> 56); break;
        case kXyzf2: case kXyz2: case kXyzf3: case kXyz3: {
            const bool f = reg == kXyzf2 || reg == kXyzf3;
            const std::uint64_t ofs = regs_[(Bits(regs_[kPrmodecont], 0, 1) ? Bits(regs_[kPrim], 9, 1)
                                                                            : Bits(regs_[kPrmode], 9, 1))
                                                ? kXyoffset2
                                                : kXyoffset1];
            const std::int32_t x = static_cast<std::int32_t>(value & 0xFFFF) - static_cast<std::int32_t>(Bits(ofs, 0, 16));
            const std::int32_t y = static_cast<std::int32_t>((value >> 16) & 0xFFFF) - static_cast<std::int32_t>(Bits(ofs, 32, 16));
            cur_.x = static_cast<float>(x) / 16.0f;
            cur_.y = static_cast<float>(y) / 16.0f;
            cur_.z = f ? static_cast<std::uint32_t>(Bits(value, 32, 24)) : static_cast<std::uint32_t>(value >> 32);
            if (f) cur_.fog = static_cast<std::uint8_t>(value >> 56);
            regs_[reg] = value;
            Kick(reg == kXyzf2 || reg == kXyz2);
            return;
        }
        case kTex0_1: case kTex0_2:
            regs_[reg] = value;
            LoadClut(value);
            return;
        case kTex2_1: case kTex2_2: {
            // TEX2 replaces PSM, CBP, CPSM, CSM, CSA and CLD of TEX0 in the same context.
            const std::uint32_t t0 = reg == kTex2_1 ? kTex0_1 : kTex0_2;
            const std::uint64_t mask = (0x3Full << 20) | (~0ull << 37);
            regs_[t0] = (regs_[t0] & ~mask) | (value & mask);
            regs_[reg] = value;
            LoadClut(regs_[t0]);
            return;
        }
        case kTrxdir: {
            regs_[reg] = value;
            const std::uint32_t dir = static_cast<std::uint32_t>(value & 3);
            transfer_pos_ = 0;
            transfer_rest_n_ = 0;
            transfer_active_ = dir == 0;
            if (dir == 2) {
                const std::uint64_t bb = regs_[kBitbltbuf], tp = regs_[kTrxpos], tr = regs_[kTrxreg];
                const std::uint32_t w = static_cast<std::uint32_t>(Bits(tr, 0, 12));
                const std::uint32_t h = static_cast<std::uint32_t>(Bits(tr, 32, 12));
                for (std::uint32_t yy = 0; yy < h; ++yy) {
                    for (std::uint32_t xx = 0; xx < w; ++xx) {
                        const std::uint32_t px = mem_.ReadPixel(static_cast<std::uint32_t>(Bits(bb, 24, 6)),
                                                                static_cast<std::uint32_t>(Bits(bb, 0, 14)),
                                                                static_cast<std::uint32_t>(Bits(bb, 16, 6)),
                                                                static_cast<std::uint32_t>(Bits(tp, 0, 11)) + xx,
                                                                static_cast<std::uint32_t>(Bits(tp, 16, 11)) + yy);
                        mem_.WritePixel(static_cast<std::uint32_t>(Bits(bb, 56, 6)),
                                        static_cast<std::uint32_t>(Bits(bb, 32, 14)),
                                        static_cast<std::uint32_t>(Bits(bb, 48, 6)),
                                        static_cast<std::uint32_t>(Bits(tp, 32, 11)) + xx,
                                        static_cast<std::uint32_t>(Bits(tp, 48, 11)) + yy, px);
                    }
                }
            } else if (dir == 1) {
                LogOnce("readback", "local->host transfer not supported");
            }
            return;
        }
        default:
            break;
    }
    regs_[reg] = value;
}

void GsState::Kick(bool draw) {
    const std::uint32_t prim = static_cast<std::uint32_t>(regs_[kPrim] & 7);
    GsVertex v = cur_;
    switch (prim) {
        case 0:  // point
            if (draw) EmitPrimitive(&v, 1);
            return;
        case 1:  // line
        case 6:  // sprite
            queue_[queued_++] = v;
            if (queued_ == 2) {
                if (draw) EmitPrimitive(queue_.data(), 2);
                queued_ = 0;
            }
            return;
        case 2:  // line strip
            queue_[queued_++] = v;
            if (queued_ == 2) {
                if (draw) EmitPrimitive(queue_.data(), 2);
                queue_[0] = queue_[1];
                queued_ = 1;
            }
            return;
        case 3:  // triangle
            queue_[queued_++] = v;
            if (queued_ == 3) {
                if (draw) EmitPrimitive(queue_.data(), 3);
                queued_ = 0;
            }
            return;
        case 4:  // triangle strip
            queue_[queued_++] = v;
            if (queued_ == 3) {
                if (draw) EmitPrimitive(queue_.data(), 3);
                queue_[0] = queue_[1];
                queue_[1] = queue_[2];
                queued_ = 2;
            }
            return;
        case 5:  // triangle fan
            if (queued_ == 0 && strip_count_ == 0) {
                fan_first_ = v;
                strip_count_ = 1;
                return;
            }
            queue_[queued_++] = v;
            if (queued_ == 2) {
                const GsVertex tri[3] = {fan_first_, queue_[0], queue_[1]};
                if (draw) EmitPrimitive(tri, 3);
                queue_[0] = queue_[1];
                queued_ = 1;
            }
            return;
        default:
            LogOnce("prim7", "PRIM type 7 (reserved) kicked");
            return;
    }
}

void GsState::AppendVertices(std::vector<GsVertex>& out, const DrawCommand& d, const GsVertex* v, int count) {
    // Flat shading (and sprites): every vertex takes the colour of the last one.
    const bool flat = !d.gouraud || d.kind == PrimKind::kSprite;
    const std::size_t first = out.size();
    out.insert(out.end(), v, v + count);
    if (flat) {
        for (std::size_t i = first; i < out.size(); ++i) {
            out[i].r = v[count - 1].r;
            out[i].g = v[count - 1].g;
            out[i].b = v[count - 1].b;
            out[i].a = v[count - 1].a;
        }
    }
}

void GsState::EmitPrimitive(const GsVertex* v, int count) {
    if (!state_dirty_ && !draws_.empty()) {
        DrawCommand& last = draws_.back();
        bool same = true;
        if (last.textured && last.texture_from_frame < 0) {
            std::uint32_t r[4];
            TexelRange(last, v, count, r);
            same = r[0] == fast_range_[0] && r[1] == fast_range_[1] && r[2] == fast_range_[2] && r[3] == fast_range_[3];
        }
        if (same) {
            AppendVertices(last.vertices, last, v, count);
            return;
        }
    }
    const std::uint64_t attr = Bits(regs_[kPrmodecont], 0, 1) ? regs_[kPrim] : regs_[kPrmode];
    const std::uint32_t prim = static_cast<std::uint32_t>(regs_[kPrim] & 7);
    const bool ctxt = Bits(attr, 9, 1) != 0;
    DrawCommand& d = building_;
    d.kind = prim == 0 ? PrimKind::kPoint : (prim <= 2 ? PrimKind::kLine : (prim == 6 ? PrimKind::kSprite : PrimKind::kTriangle));
    d.gouraud = Bits(attr, 3, 1) != 0;
    d.textured = Bits(attr, 4, 1) != 0;
    d.fog = Bits(attr, 5, 1) != 0;
    d.blend = Bits(attr, 6, 1) != 0;
    d.aa1 = Bits(attr, 7, 1) != 0;
    d.fst = Bits(attr, 8, 1) != 0;
    d.tex0 = regs_[ctxt ? kTex0_2 : kTex0_1];
    d.tex1 = regs_[ctxt ? kTex1_2 : kTex1_1];
    d.clamp = regs_[ctxt ? kClamp2 : kClamp1];
    d.alpha = regs_[ctxt ? kAlpha2 : kAlpha1];
    d.test = regs_[ctxt ? kTest2 : kTest1];
    d.frame = regs_[ctxt ? kFrame2 : kFrame1];
    d.zbuf = regs_[ctxt ? kZbuf2 : kZbuf1];
    d.scissor = regs_[ctxt ? kScissor2 : kScissor1];
    d.fba = regs_[ctxt ? kFba2 : kFba1];
    d.texa = regs_[kTexa];
    d.fogcol = regs_[kFogcol];
    d.colclamp = (regs_[kColclamp] & 1) != 0;
    d.pabe = (regs_[kPabe] & 1) != 0;
    d.dthe = (regs_[kDthe] & 1) != 0;
    d.texture.reset();
    d.texture_from_frame = -1;
    d.texture_guest_source = 0;
    d.texture_guest_width = 0;
    if (d.textured) {
        const std::uint32_t tbp = static_cast<std::uint32_t>(Bits(d.tex0, 0, 14));
        if (frame_query_ && frame_query_(tbp)) {
            d.texture_from_frame = static_cast<std::int32_t>(tbp);
        } else {
            d.texture = DecodeTexture(d, v, count);
        }
        if (!upload_source_.empty()) {
            const auto src = upload_source_.find(tbp);
            if (src != upload_source_.end()) {
                d.texture_guest_source = src->second.first;
                d.texture_guest_width = src->second.second;
            }
        }
    }
    if (d.textured && d.texture_from_frame < 0) TexelRange(d, v, count, fast_range_);
    const bool merge = !draws_.empty() && SameState(draws_.back(), d);
    if (merge) {
        AppendVertices(draws_.back().vertices, d, v, count);
    } else {
        d.vertices.clear();
        AppendVertices(d.vertices, d, v, count);
        draws_.push_back(std::move(d));
        d = DrawCommand{};
    }
    state_dirty_ = false;
}

void GsState::TexelRange(const DrawCommand& d, const GsVertex* v, int count, std::uint32_t r[4]) const {
    const std::uint64_t t = d.tex0;
    const std::uint32_t tw = std::min<std::uint32_t>(static_cast<std::uint32_t>(Bits(t, 26, 4)), 10);
    const std::uint32_t th = std::min<std::uint32_t>(static_cast<std::uint32_t>(Bits(t, 30, 4)), 10);
    const std::uint32_t size_u = 1u << tw, size_v = 1u << th;
    // Triangles (3D models) take the whole texture: one decode per texture instead of one per
    // triangle, so a model's triangles share a texture and merge into one draw.
    if (d.kind == PrimKind::kTriangle) {
        r[0] = 0;
        r[1] = 0;
        r[2] = size_u;
        r[3] = size_v;
        return;
    }
    // Texel rectangle the primitive can reach (+1 for bilinear neighbours).
    float umin = 1e9f, umax = -1e9f, vmin = 1e9f, vmax = -1e9f;
    for (int i = 0; i < count; ++i) {
        float uu, vv;
        if (d.fst) {
            uu = v[i].u;
            vv = v[i].v;
        } else {
            const float q = v[i].q != 0.0f ? v[i].q : 1.0f;
            uu = v[i].s / q * static_cast<float>(size_u);
            vv = v[i].t / q * static_cast<float>(size_v);
        }
        umin = std::min(umin, uu);
        umax = std::max(umax, uu);
        vmin = std::min(vmin, vv);
        vmax = std::max(vmax, vv);
    }
    const std::uint32_t wms = static_cast<std::uint32_t>(Bits(d.clamp, 0, 2));
    const std::uint32_t wmt = static_cast<std::uint32_t>(Bits(d.clamp, 2, 2));
    // Bilinear filtering (TEX1.MMAG) reads texels floor(c - 0.5) and floor(c - 0.5) + 1 (the
    // shader's footprint); nearest reads floor(c).
    const bool bilinear = Bits(d.tex1, 5, 1) != 0;
    auto range = [bilinear](float lo, float hi, std::uint32_t size, std::uint32_t mode, std::uint32_t& a, std::uint32_t& b) {
        const bool repeat = mode == 0 || mode == 3;
        const std::int64_t l = static_cast<std::int64_t>(std::floor(bilinear ? lo - 0.5f : lo));
        const std::int64_t h = static_cast<std::int64_t>(std::floor(bilinear ? hi - 0.5f : hi)) + (bilinear ? 1 : 0);
        if (repeat && (l < 0 || h >= static_cast<std::int64_t>(size))) {
            a = 0;
            b = size;
            return;
        }
        a = static_cast<std::uint32_t>(std::clamp<std::int64_t>(l, 0, size - 1));
        b = static_cast<std::uint32_t>(std::clamp<std::int64_t>(h, 0, size - 1)) + 1;
    };
    range(umin, umax, size_u, wms, r[0], r[2]);
    range(vmin, vmax, size_v, wmt, r[1], r[3]);
}

std::shared_ptr<const DecodedTexture> GsState::DecodeTexture(const DrawCommand& d, const GsVertex* v, int count) {
    const std::uint64_t t = d.tex0;
    const std::uint32_t tbp = static_cast<std::uint32_t>(Bits(t, 0, 14));
    const std::uint32_t tbw = static_cast<std::uint32_t>(Bits(t, 14, 6));
    const std::uint32_t psm = static_cast<std::uint32_t>(Bits(t, 20, 6));
    const std::uint32_t tw = std::min<std::uint32_t>(static_cast<std::uint32_t>(Bits(t, 26, 4)), 10);
    const std::uint32_t th = std::min<std::uint32_t>(static_cast<std::uint32_t>(Bits(t, 30, 4)), 10);
    const std::uint32_t cpsm = static_cast<std::uint32_t>(Bits(t, 51, 4));
    const std::uint32_t csa = static_cast<std::uint32_t>(Bits(t, 56, 5));
    const std::uint32_t size_u = 1u << tw, size_v = 1u << th;
    if (BitsPerPixel(psm) == 0) {
        LogOnce("tex-psm", "texture PSM 0x%llx not supported", psm);
        return nullptr;
    }
    std::uint32_t r[4];
    TexelRange(d, v, count, r);
    const std::uint32_t u0 = r[0], v0 = r[1], u1 = r[2], v1 = r[3];

    const bool paletted = psm == kPsmT8 || psm == kPsmT8H || psm == kPsmT4 || psm == kPsmT4HL || psm == kPsmT4HH;
    std::uint64_t clut_hash = 0;
    if (paletted) {
        const bool eight = psm == kPsmT8 || psm == kPsmT8H;
        const std::uint32_t base = eight ? 0 : (cpsm == 0 ? (csa & 15) : csa) * 16;
        const std::uint32_t args = base | (eight ? 0x1000u : 0u) | cpsm << 16;
        if (clut_hash_gen_ != clut_gen_ || clut_hash_args_ != args) {
            clut_hash_ = Fnv(clut_.data() + base, eight ? 256 : 16) ^ cpsm;
            clut_hash_gen_ = clut_gen_;
            clut_hash_args_ = args;
        }
        clut_hash = clut_hash_;
    }
    const std::uint64_t gen = mem_.RegionHash(psm, tbp, tbw, size_u, size_v);
    const auto key = std::make_tuple(t & 0x1FFFFFFFFFFFFFFFull, clut_hash ^ (d.texa * 31), gen, u0, v0, u1, v1);
    if (last_key_valid_ && key == last_key_) return last_texture_;
    if (auto it = texture_cache_.find(key); it != texture_cache_.end()) {
        last_key_valid_ = true;
        last_key_ = key;
        last_texture_ = it->second;
        return it->second;
    }
    if (texture_cache_.size() > 4096) texture_cache_.clear();

    auto tex = std::make_shared<DecodedTexture>();
    tex->origin_u = u0;
    tex->origin_v = v0;
    tex->width = u1 - u0;
    tex->height = v1 - v0;
    tex->tex_width = size_u;
    tex->tex_height = size_v;
    tex->rgba.resize(static_cast<std::size_t>(tex->width) * tex->height * 4);
    const std::uint32_t ta0 = static_cast<std::uint32_t>(Bits(d.texa, 0, 8));
    const std::uint32_t ta1 = static_cast<std::uint32_t>(Bits(d.texa, 32, 8));
    const bool aem = Bits(d.texa, 15, 1) != 0;
    auto from16 = [&](std::uint32_t p) {
        const std::uint32_t r = (p & 0x1F) << 3, g = ((p >> 5) & 0x1F) << 3, b = ((p >> 10) & 0x1F) << 3;
        std::uint32_t a = (p & 0x8000) ? ta1 : ta0;
        if (aem && (p & 0x7FFF) == 0) a = 0;
        return r | g << 8 | b << 16 | a << 24;
    };
    for (std::uint32_t y = 0; y < tex->height; ++y) {
        for (std::uint32_t x = 0; x < tex->width; ++x) {
            const std::uint32_t raw = mem_.ReadPixel(psm, tbp, tbw, u0 + x, v0 + y);
            std::uint32_t c;
            switch (psm) {
                case kPsmCt32: c = raw; break;
                case kPsmCt24: {
                    std::uint32_t a = ta0;
                    if (aem && (raw & 0xFFFFFF) == 0) a = 0;
                    c = (raw & 0xFFFFFF) | a << 24;
                    break;
                }
                case kPsmCt16: case kPsmCt16S: c = from16(raw); break;
                case kPsmT8: case kPsmT8H: c = cpsm == 0 ? clut_[raw & 255] : from16(clut_[raw & 255]); break;
                default: {  // 4-bit
                    const std::uint32_t base = (cpsm == 0 ? (csa & 15) : csa) * 16;
                    const std::uint32_t e = clut_[(base + (raw & 15)) & 511];
                    c = cpsm == 0 ? e : from16(e);
                    break;
                }
            }
            std::memcpy(&tex->rgba[(static_cast<std::size_t>(y) * tex->width + x) * 4], &c, 4);
        }
    }
    texture_cache_[key] = tex;
    last_key_valid_ = true;
    last_key_ = key;
    last_texture_ = tex;
    return tex;
}

}  // namespace ac04::render::gs
