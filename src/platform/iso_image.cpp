#include "platform/iso_image.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <sstream>
#include <system_error>
#include <vector>

#include "platform/paths.h"
#include "platform/sha1.h"

namespace ac04::platform {
namespace {

using json = nlohmann::json;

json LoadCache(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return json::object();
    std::stringstream ss;
    ss << in.rdbuf();
    json j = json::parse(ss.str(), nullptr, false);
    return j.is_object() ? j : json::object();
}

void SaveCache(const std::filesystem::path& file, const json& j) {
    std::filesystem::path tmp = file;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return;
        out << j.dump(2) << "\n";
    }
    std::error_code ec;
    std::filesystem::rename(tmp, file, ec);
}

}  // namespace

IsoCheck IsoImage::OpenAndQuickCheck(const std::filesystem::path& path) {
    IsoCheck c;
    path_ = path;
    if (path.empty()) {
        c.result = IsoCheckResult::NoPathConfigured;
        return c;
    }
    file_ = std::make_shared<FileReader>();
    std::string error;
    if (!file_->Open(path, error)) {
        c.result = IsoCheckResult::CannotOpen;
        c.detail = error;
        return c;
    }
    if (!iso_.Open(std::make_shared<FileSectorSource>(file_), error)) {
        c.result = IsoCheckResult::NotAnIso;
        c.detail = error;
        return c;
    }
    const auto& v = iso_.Volume();
    if (file_->Size() != SupportedImage::kSizeBytes || v.system_id != SupportedImage::kSystemId ||
        v.volume_id != SupportedImage::kVolumeId) {
        c.result = IsoCheckResult::WrongImage;
        c.detail = "size " + std::to_string(file_->Size()) + " bytes, system id '" + v.system_id +
                   "', volume id '" + v.volume_id + "' (expected " +
                   std::to_string(SupportedImage::kSizeBytes) + " bytes, '" +
                   SupportedImage::kSystemId + "', '" + SupportedImage::kVolumeId + "')";
    }
    return c;
}

IsoCheck IsoImage::FullCheck(const std::filesystem::path& cache_file, bool force,
                             const std::function<void(double)>& progress,
                             const std::atomic<bool>& cancel) {
    IsoCheck c;
    std::error_code ec;
    const std::string key = PathToUtf8(std::filesystem::absolute(path_, ec));
    json cache = LoadCache(cache_file);
    if (!force && cache.contains(key)) {
        const json& e = cache[key];
        if (e.value("size", 0ull) == file_->Size() && e.value("mtime", 0ull) == file_->ModificationTime() &&
            e.value("sha1", std::string()).size() == 40) {
            c.found_sha1 = e["sha1"].get<std::string>();
            c.from_cache = true;
        }
    }
    if (!c.from_cache) {
        constexpr std::uint64_t kChunk = 8ull << 20;
        std::vector<std::uint8_t> buf(kChunk);
        Sha1 sha;
        const std::uint64_t total = file_->Size();
        std::uint64_t done = 0, next_report = 0;
        while (done < total) {
            if (cancel.load()) {
                c.result = IsoCheckResult::Cancelled;
                return c;
            }
            const std::uint64_t n = total - done < kChunk ? total - done : kChunk;
            if (!file_->Read(done, buf.data(), n)) {
                c.result = IsoCheckResult::CannotOpen;
                c.detail = "read error at byte " + std::to_string(done);
                return c;
            }
            sha.Update(buf.data(), static_cast<size_t>(n));
            done += n;
            if (done >= next_report) {
                if (progress) progress(static_cast<double>(done) / static_cast<double>(total));
                next_report = done + (64ull << 20);
            }
        }
        c.found_sha1 = sha.FinalHex();
        cache[key] = {{"size", file_->Size()}, {"mtime", file_->ModificationTime()}, {"sha1", c.found_sha1}};
        SaveCache(cache_file, cache);
    }
    if (c.found_sha1 != SupportedImage::kImageSha1) {
        c.result = IsoCheckResult::HashMismatch;
        c.detail = "SHA-1 " + c.found_sha1 + " (expected " + SupportedImage::kImageSha1 + ")";
    }
    return c;
}

std::string DescribeIsoProblem(const IsoCheck& check, const std::filesystem::path& image,
                               const std::filesystem::path& settings_file) {
    const std::string where = "Set \"paths\": { \"iso\": \"...\" } in\n" + PathToUtf8(settings_file) +
                              "\nor start the port with --iso <path>.";
    const std::string expected =
        "The port supports one disc image: Ace Combat 04: Shattered Skies, NTSC-U (SLUS-20152), "
        "as a 2048-byte-sector ISO image of " + std::to_string(SupportedImage::kSizeBytes) + " bytes.";
    switch (check.result) {
        case IsoCheckResult::Ok:
            return "";
        case IsoCheckResult::NoPathConfigured:
            return "No disc image is configured.\n\n" + where + "\n\n" + expected;
        case IsoCheckResult::CannotOpen:
            return "The disc image cannot be read:\n" + PathToUtf8(image) + "\n(" + check.detail + ")\n\n" + where;
        case IsoCheckResult::NotAnIso:
            return "The file is not a supported disc image:\n" + PathToUtf8(image) + "\n(" + check.detail +
                   ")\n\n" + expected + " Raw .bin dumps must be converted to ISO first.";
        case IsoCheckResult::WrongImage:
            return "This is not the supported disc image:\n" + PathToUtf8(image) + "\nFound: " + check.detail +
                   "\n\n" + expected;
        case IsoCheckResult::HashMismatch:
            return "The disc image does not match the supported version (checksum differs):\n" +
                   PathToUtf8(image) + "\n" + check.detail + "\n\n" + expected +
                   " Make a fresh, unmodified dump of your disc.";
        case IsoCheckResult::Cancelled:
            return "Verification of the disc image was cancelled.";
    }
    return "Unknown disc image problem.";
}

}  // namespace ac04::platform
