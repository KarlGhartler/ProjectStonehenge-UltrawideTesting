#include "sce/sce_host.h"

#include <string>

#include "platform/log.h"

namespace ac04::sce {

SceHost& Host() {
    static SceHost host;
    return host;
}

DmaStats& Dma() {
    static DmaStats stats;
    return stats;
}

std::vector<u8>& IopRam() {
    static std::vector<u8> ram(2u * 1024u * 1024u, 0);
    return ram;
}

void LogUnimplemented(const char* name) {
    const std::string key = std::string("sce:") + name;
    log::WriteOnce(key.c_str(), log::Level::Warning, "sce", "%s: not implemented (stub returns an error)", name);
}

}  // namespace ac04::sce
