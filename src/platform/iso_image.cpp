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

MessageText DescribeIsoProblem(const IsoCheck& check, const std::filesystem::path& image,
                               const std::filesystem::path& settings_file) {
    const std::string game = "Ace Combat 04: Shattered Skies, North America (NTSC-U, SLUS-20152)";
    // Size with thousands separators (3,554,705,408 bytes).
    std::string bytes = std::to_string(SupportedImage::kSizeBytes);
    for (int i = static_cast<int>(bytes.size()) - 3; i > 0; i -= 3) bytes.insert(static_cast<std::size_t>(i), ",");
    const std::string expected = game + ", ISO image of " + bytes + " bytes";
    const std::string choose = "Click \"Select ISO...\" and pick the .iso file of your disc. The choice is saved.";
    const std::string command_line = "Or start the port with  --iso \"<path of the .iso file>\"";
    MessageText m;
    switch (check.result) {
        case IsoCheckResult::Ok:
            return m;
        case IsoCheckResult::NoPathConfigured:
            m.headline = "No disc image selected";
            m.explanation = "The port runs the game from an ISO image of your own copy of " + game + ".";
            m.steps = {choose, command_line};
            m.details = {{"Supported disc", expected}, {"Settings file", PathToUtf8(settings_file)}};
            return m;
        case IsoCheckResult::CannotOpen:
            m.headline = "The disc image cannot be opened";
            m.explanation = "The file set as the disc image could not be read (" + check.detail + ").";
            m.steps = {"Check that the file still exists and that no other program has it open.",
                       "Or choose the disc image again: click \"Select ISO...\"."};
            m.details = {{"Disc image", PathToUtf8(image)}, {"Settings file", PathToUtf8(settings_file)}};
            return m;
        case IsoCheckResult::NotAnIso:
            m.headline = "This file is not an ISO disc image";
            m.explanation = "It does not hold the file system of a PlayStation 2 disc (" + check.detail + ").";
            m.steps = {"Choose the .iso image of the disc: click \"Select ISO...\".",
                       "Images in other formats (.bin/.cue, .img, .chd, .cso) must be converted to .iso first."};
            m.details = {{"Selected file", PathToUtf8(image)}, {"Supported disc", expected}};
            return m;
        case IsoCheckResult::WrongImage:
            m.headline = "This is not the supported version of the game";
            m.explanation = "The port supports one disc only: " + game + ".";
            m.steps = {"Choose an ISO image of that disc: click \"Select ISO...\"."};
            m.details = {{"Selected file", PathToUtf8(image)}, {"Found", check.detail}, {"Supported disc", expected}};
            return m;
        case IsoCheckResult::HashMismatch:
            m.headline = "The disc image is damaged or modified";
            m.explanation = "Its checksum differs from the original disc: the image may be incomplete, damaged, "
                            "patched, or from another release.";
            m.steps = {"Make a new, unmodified 1:1 dump of your disc as an ISO image.",
                       "Choose it: click \"Select ISO...\"."};
            m.details = {{"Selected file", PathToUtf8(image)}, {"Checksum", check.detail}, {"Supported disc", expected}};
            return m;
        case IsoCheckResult::Cancelled:
            m.headline = "Disc image check cancelled";
            m.explanation = "The port was closed while it was checking the disc image.";
            return m;
    }
    m.headline = "Unknown disc image problem";
    return m;
}

}  // namespace ac04::platform
