#include "common/guest_call.h"

#include <cstdio>
#include <unordered_map>

#include "platform/log.h"

namespace ac04::guest {
namespace {

struct Entry {
    GuestThunk thunk;
    const char* name;
};

// Function-local static: registration happens from static constructors in other files.
std::unordered_map<u32, Entry>& Table() {
    static std::unordered_map<u32, Entry> table;
    return table;
}

}  // namespace

void RegisterGuestFunction(u32 address, GuestThunk thunk, const char* name) {
    auto [it, inserted] = Table().emplace(address, Entry{thunk, name});
    if (!inserted) {
        AC04_LOG_ERROR("guest", "two functions registered at 0x%08X: %s and %s", address, it->second.name, name);
    }
}

bool IsGuestFunctionRegistered(u32 address) { return Table().count(address) != 0; }

const char* GuestFunctionName(u32 address) {
    auto it = Table().find(address);
    return it == Table().end() ? nullptr : it->second.name;
}

size_t RegisteredGuestFunctionCount() { return Table().size(); }

GuestResult CallGuestRaw(u32 address, const GuestArgs& args) {
    auto it = Table().find(address);
    if (it == Table().end()) {
        char key[40];
        std::snprintf(key, sizeof(key), "guest-call-%08X", address);
        log::WriteOnce(key, log::Level::Error, "guest",
                       "indirect call to 0x%08X: no translated function registered there; returning 0",
                       address);
        return GuestResult{};
    }
    return it->second.thunk(args);
}

}  // namespace ac04::guest
