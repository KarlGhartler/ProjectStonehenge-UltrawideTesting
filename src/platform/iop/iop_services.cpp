#include "platform/iop/iop_services.h"

#include <array>
#include <cstring>
#include <string>

#include "platform/log.h"

namespace ac04::platform::iop {
namespace {

void PutWord(std::uint8_t* reply, std::uint32_t reply_size, std::uint32_t offset, std::uint32_t value) {
    if (offset + 4 <= reply_size) std::memcpy(reply + offset, &value, 4);
}

void LogUnhandled(const char* service, std::uint32_t fno) {
    const std::string key = std::string("iop:") + service + ":" + std::to_string(fno);
    log::WriteOnce(key.c_str(), log::Level::Warning, "iop", "%s: fno %u not implemented (accepted)", service, fno);
}

// SOUND.IRX fno 1 (docs/subsystems/iop-sound.md "Command records" / "Reply"): the batch is
// {count, records...} with a fixed size per id; the reply is {count = voice records + 2, one
// 12-byte record per command with ids 1-4/6-9 {id, request id, u8 tag, u8 status 1}, the transfer
// record {10, last finished ticket, done 1}, the voice-status record {0xF, output mode, 48 bytes}}.
// Stand-in limits (no mixer yet): BD transfers finish at once; keyed-on voices stay "playing"
// until keyed off. The reverb work-area address of id 6 is 0x001DB7C0, measured in PCSX2 for the
// only id-6 command the game sends (core 1, mode 2; scenestep_first_2.p2s, docs/port/validation.md
// E-1): the effect end address 0x001FFFFF minus the 0x24840-byte area, plus 1.
constexpr std::uint32_t kReverbWorkArea = 0x001DB7C0u;

class SoundService final : public RpcService {
public:
    std::uint32_t RpcNumber() const override { return 0x81000101u; }
    const char* Name() const override { return "SOUND"; }
    void Call(std::uint32_t fno, const std::uint8_t* send, std::uint32_t send_size, std::uint8_t* reply,
              std::uint32_t reply_size) override {
        if (fno == 0) {
            std::memset(reply, 0, reply_size);
            PutWord(reply, reply_size, 0, 2);
            voice_state_.fill(0);
            return;
        }
        if (fno != 1) {
            LogUnhandled(Name(), fno);
            return;
        }
        std::memset(reply, 0, reply_size);
        const std::uint32_t count = Word(send, send_size, 0);
        std::uint32_t in = 4;
        std::uint32_t out = 4;
        std::uint32_t records = 0;
        for (std::uint32_t i = 0; i < count && in + 4 <= send_size; ++i) {
            const std::uint32_t id = Word(send, send_size, in);
            std::uint32_t size;
            switch (id) {
                case 1: case 2: case 3: case 4: size = 0x48; break;
                case 5: case 0xB: case 0xC: case 0xD: size = 12; break;
                case 6: case 7: case 8: case 9: size = 16; break;
                case 0xA: size = 20; break;
                case 0xE: size = 8; break;
                default:
                    LogUnhandled("SOUND command", id);
                    size = 0;
                    break;
            }
            if (size == 0 || in + size > send_size) break;
            const std::uint8_t* rec = send + in;
            const std::uint8_t voice = rec[0x12 < size ? 0x12 : 0];
            switch (id) {
                case 1:
                case 3:
                    if (voice < 48) voice_state_[voice] = 1;
                    break;
                case 4:
                    if (voice < 48) voice_state_[voice] = 0;
                    break;
                case 5: {
                    const std::uint32_t m0 = Word(rec, size, 4), m1 = Word(rec, size, 8);
                    for (std::uint32_t v = 0; v < 24; ++v) {
                        if (m0 & (1u << v)) voice_state_[v] = 0;
                        if (m1 & (1u << v)) voice_state_[24 + v] = 0;
                    }
                    break;
                }
                case 0xA:
                    last_ticket_ = Word(rec, size, 0x10);
                    break;
                case 0xE:
                    output_mode_ = Word(rec, size, 4);
                    break;
                default:
                    break;
            }
            if ((id >= 1 && id <= 4) || (id >= 6 && id <= 9)) {
                PutWord(reply, reply_size, out, id);
                PutWord(reply, reply_size, out + 4, id == 6 ? kReverbWorkArea : Word(rec, size, 4));
                if (out + 10 <= reply_size) {
                    reply[out + 8] = size > 0x0E ? rec[0x0E] : 0;
                    reply[out + 9] = 1;
                }
                out += 12;
                ++records;
            }
            in += size;
        }
        PutWord(reply, reply_size, out, 10);
        PutWord(reply, reply_size, out + 4, last_ticket_);
        PutWord(reply, reply_size, out + 8, 1);
        out += 12;
        PutWord(reply, reply_size, out, 0xF);
        PutWord(reply, reply_size, out + 4, output_mode_);
        if (out + 8 + 48 <= reply_size) std::memcpy(reply + out + 8, voice_state_.data(), 48);
        PutWord(reply, reply_size, 0, records + 2);
    }

private:
    static std::uint32_t Word(const std::uint8_t* p, std::uint32_t size, std::uint32_t offset) {
        std::uint32_t v = 0;
        if (offset + 4 <= size) std::memcpy(&v, p + offset, 4);
        return v;
    }
    std::array<std::uint8_t, 48> voice_state_{};
    std::uint32_t last_ticket_ = 0;
    std::uint32_t output_mode_ = 0;
};

class VagService final : public RpcService {
public:
    std::uint32_t RpcNumber() const override { return 0x81000102u; }
    const char* Name() const override { return "VAG"; }
    void Call(std::uint32_t fno, const std::uint8_t*, std::uint32_t, std::uint8_t* reply, std::uint32_t reply_size) override {
        if (fno > 1) {
            LogUnhandled(Name(), fno);
            return;
        }
        std::memset(reply, 0, reply_size);
        PutWord(reply, reply_size, 0x00, 0x0008DA40u);
        for (std::uint32_t ch = 0; ch < 4; ++ch) PutWord(reply, reply_size, 0x24 + 4 * ch, 1);
        PutWord(reply, reply_size, 0x50, 0x200);
        PutWord(reply, reply_size, 0x60, 0x200);
    }
};

class UsbJoyService final : public RpcService {
public:
    std::uint32_t RpcNumber() const override { return 0x80000210u; }
    const char* Name() const override { return "USBJOY"; }
    void Call(std::uint32_t fno, const std::uint8_t*, std::uint32_t, std::uint8_t* reply, std::uint32_t reply_size) override {
        if (fno != 2) {
            LogUnhandled(Name(), fno);
            return;
        }
        std::memset(reply, 0, reply_size);
        for (std::uint32_t dev = 0; dev < 2; ++dev) {
            const std::uint32_t base = 0x08 + dev * 0x28;
            for (std::uint32_t i = 0; i < 6; ++i) PutWord(reply, reply_size, base + 4 * i, 0x80);
            PutWord(reply, reply_size, base + 0x20, 0xFF);
            PutWord(reply, reply_size, base + 0x24, 0xFF);
        }
    }
};

}  // namespace

std::unique_ptr<RpcService> MakeSoundService() { return std::make_unique<SoundService>(); }
std::unique_ptr<RpcService> MakeVagService() { return std::make_unique<VagService>(); }
std::unique_ptr<RpcService> MakeUsbJoyService() { return std::make_unique<UsbJoyService>(); }

}  // namespace ac04::platform::iop
