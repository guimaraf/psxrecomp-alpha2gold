// disc_extractor.h — Native C++ ISO9660 / CUE / BIN PS1 Disc Extractor
//
// Integrated into the RmlUi launcher to enable zero-install first-run setup.
// Reads Mode 2 Form 1 raw sectors (2352 bytes) and cooked ISO sectors (2048 bytes)
// without calling external Python or MSYS2 tools.

#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace psx_launcher {

struct DiscFileInfo {
    std::string path;
    uint32_t    lba          = 0;
    uint32_t    size         = 0;
    bool        is_directory = false;
};

class DiscExtractor {
public:
    using ProgressFn = std::function<void(float fraction, const std::string& status)>;

    DiscExtractor();
    ~DiscExtractor();

    // Open a disc image (.cue, .bin, or .iso)
    bool open(const std::filesystem::path& path);
    void close();
    bool is_open() const;

    // Disc metadata
    std::string volume_id() const;
    std::string detected_serial() const;
    uint32_t    sector_size() const;

    // Directory inspection
    std::vector<DiscFileInfo> list_root() const;
    std::vector<DiscFileInfo> list_directory(const std::string& dir_name) const;

    // Extraction primitives
    bool extract_file_by_lba(uint32_t lba, uint32_t size,
                             const std::filesystem::path& dest_path,
                             ProgressFn progress = nullptr);

    bool extract_primary_exe(const std::filesystem::path& dest_path,
                             ProgressFn progress = nullptr);

    int  extract_directory_files(const std::string& iso_dir,
                                 const std::filesystem::path& dest_dir,
                                 ProgressFn progress = nullptr);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace psx_launcher
