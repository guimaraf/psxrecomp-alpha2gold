// disc_extractor.cpp — see disc_extractor.h.
//
// Native C++ ISO9660 reader and extractor for PS1 Mode 2 Form 1 discs.
// Self-contained implementation using std::ifstream and std::filesystem.

#include "disc_extractor.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace psx_launcher {

namespace {

constexpr size_t RAW_SECTOR_SIZE   = 2352;
constexpr size_t FORM1_PAYLOAD_SZ  = 2048;
constexpr size_t FORM1_OFFSET_2352 = 24;
constexpr size_t PVD_LBA           = 16;

uint32_t read_le32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) |
          (static_cast<uint32_t>(p[1]) << 8) |
          (static_cast<uint32_t>(p[2]) << 16) |
          (static_cast<uint32_t>(p[3]) << 24);
}

std::string uppercase(std::string s) {
    for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

fs::path resolve_cue_binary(const fs::path& cue_path) {
    if (uppercase(cue_path.extension().string()) != ".CUE") {
        return cue_path;
    }

    std::ifstream cue(cue_path);
    if (!cue.is_open()) return cue_path;

    std::string line;
    while (std::getline(cue, line)) {
        std::string up = uppercase(line);
        size_t file_pos = up.find("FILE");
        if (file_pos == std::string::npos || up.find("BINARY") == std::string::npos) {
            continue;
        }

        size_t q1 = line.find('"', file_pos);
        size_t q2 = (q1 == std::string::npos) ? std::string::npos : line.find('"', q1 + 1);
        std::string bin_name;
        if (q1 != std::string::npos && q2 != std::string::npos && q2 > q1 + 1) {
            bin_name = line.substr(q1 + 1, q2 - q1 - 1);
        } else {
            std::istringstream iss(line.substr(file_pos + 4));
            iss >> bin_name;
        }

        if (!bin_name.empty()) {
            fs::path bin_path(bin_name);
            if (bin_path.is_relative()) {
                return cue_path.parent_path() / bin_path;
            }
            return bin_path;
        }
    }
    return cue_path;
}

} // namespace

struct DiscExtractor::Impl {
    fs::path      bin_path;
    std::ifstream file;
    uint32_t      sector_size    = RAW_SECTOR_SIZE;
    uint32_t      payload_offset = FORM1_OFFSET_2352;
    uint32_t      root_lba       = 0;
    uint32_t      root_size      = 0;
    std::string   vol_id;
    std::string   detected_ser;

    bool read_payload(uint32_t lba, uint32_t size, uint8_t* dst) {
        if (!file.is_open()) return false;
        uint32_t sectors = (size + FORM1_PAYLOAD_SZ - 1) / FORM1_PAYLOAD_SZ;
        uint32_t remaining = size;
        uint8_t  sec[RAW_SECTOR_SIZE];

        for (uint32_t i = 0; i < sectors; i++) {
            uint64_t offset = (static_cast<uint64_t>(lba + i) * sector_size) + payload_offset;
            file.clear();
            file.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
            if (!file.good()) return false;

            file.read(reinterpret_cast<char*>(sec), FORM1_PAYLOAD_SZ);
            if (static_cast<size_t>(file.gcount()) != FORM1_PAYLOAD_SZ) return false;

            uint32_t take = (remaining < FORM1_PAYLOAD_SZ) ? remaining : FORM1_PAYLOAD_SZ;
            std::memcpy(dst + (i * FORM1_PAYLOAD_SZ), sec, take);
            remaining -= take;
        }
        return true;
    }

    std::vector<DiscFileInfo> read_directory(uint32_t dir_lba, uint32_t dir_size) {
        std::vector<DiscFileInfo> entries;
        if (dir_size == 0) return entries;

        std::vector<uint8_t> dir_data(dir_size);
        if (!read_payload(dir_lba, dir_size, dir_data.data())) {
            return entries;
        }

        uint32_t offset = 0;
        while (offset < dir_size) {
            uint8_t rec_len = dir_data[offset];
            if (rec_len == 0) {
                // Pad to next sector boundary
                offset = ((offset / FORM1_PAYLOAD_SZ) + 1) * FORM1_PAYLOAD_SZ;
                continue;
            }
            if (offset + rec_len > dir_size || rec_len < 33) break;

            const uint8_t* rec = dir_data.data() + offset;
            uint32_t lba = read_le32(rec + 2);
            uint32_t sz  = read_le32(rec + 10);
            uint8_t  flags = rec[25];
            uint8_t  name_len = rec[32];

            if (name_len > 0 && offset + 33 + name_len <= dir_size) {
                std::string raw_name(reinterpret_cast<const char*>(rec + 33), name_len);
                // Filter out "." and ".."
                if (!raw_name.empty() && raw_name[0] != '\0' && raw_name[0] != '\1') {
                    // Strip ";1" version
                    size_t semi = raw_name.find(';');
                    if (semi != std::string::npos) {
                        raw_name.resize(semi);
                    }

                    DiscFileInfo item;
                    item.path = raw_name;
                    item.lba  = lba;
                    item.size = sz;
                    item.is_directory = (flags & 0x02) != 0;
                    entries.push_back(std::move(item));
                }
            }
            offset += rec_len;
        }
        return entries;
    }
};

DiscExtractor::DiscExtractor() : impl_(std::make_unique<Impl>()) {}
DiscExtractor::~DiscExtractor() = default;

bool DiscExtractor::open(const fs::path& path) {
    close();

    impl_->bin_path = resolve_cue_binary(path);
    impl_->file.open(impl_->bin_path, std::ios::binary);
    if (!impl_->file.is_open()) {
        return false;
    }

    uint8_t sec[RAW_SECTOR_SIZE];

    // Try raw 2352-byte sector (PS1 Mode 2 Form 1: user data at +24)
    impl_->file.seekg(static_cast<std::streamoff>(PVD_LBA * RAW_SECTOR_SIZE), std::ios::beg);
    impl_->file.read(reinterpret_cast<char*>(sec), RAW_SECTOR_SIZE);
    if (impl_->file.gcount() == static_cast<std::streamsize>(RAW_SECTOR_SIZE)) {
        if (sec[FORM1_OFFSET_2352 + 1] == 'C' &&
            sec[FORM1_OFFSET_2352 + 2] == 'D' &&
            sec[FORM1_OFFSET_2352 + 3] == '0' &&
            sec[FORM1_OFFSET_2352 + 4] == '0' &&
            sec[FORM1_OFFSET_2352 + 5] == '1') {
            impl_->sector_size    = RAW_SECTOR_SIZE;
            impl_->payload_offset = FORM1_OFFSET_2352;
            goto parsed_pvd;
        }
    }

    // Try 2048-byte cooked ISO sector
    impl_->file.clear();
    impl_->file.seekg(static_cast<std::streamoff>(PVD_LBA * FORM1_PAYLOAD_SZ), std::ios::beg);
    impl_->file.read(reinterpret_cast<char*>(sec), FORM1_PAYLOAD_SZ);
    if (impl_->file.gcount() == static_cast<std::streamsize>(FORM1_PAYLOAD_SZ)) {
        if (sec[1] == 'C' && sec[2] == 'D' && sec[3] == '0' && sec[4] == '0' && sec[5] == '1') {
            impl_->sector_size    = FORM1_PAYLOAD_SZ;
            impl_->payload_offset = 0;
            goto parsed_pvd;
        }
    }

    close();
    return false;

parsed_pvd:;
    const uint8_t* pvd = sec + impl_->payload_offset;
    const uint8_t* root_rec = pvd + 156;
    impl_->root_lba  = read_le32(root_rec + 2);
    impl_->root_size = read_le32(root_rec + 10);

    // Volume identifier (bytes 40..71)
    std::string vid(reinterpret_cast<const char*>(pvd + 40), 32);
    while (!vid.empty() && (vid.back() == ' ' || vid.back() == '\0')) {
        vid.pop_back();
    }
    impl_->vol_id = vid;

    // Check for SYSTEM.CNF to detect serial
    auto root_items = list_root();
    for (const auto& item : root_items) {
        if (!item.is_directory && uppercase(item.path) == "SYSTEM.CNF" && item.size < 4096) {
            std::vector<uint8_t> cnf_data(item.size + 1, 0);
            if (impl_->read_payload(item.lba, item.size, cnf_data.data())) {
                std::string cnf_text(reinterpret_cast<char*>(cnf_data.data()), item.size);
                std::istringstream iss(cnf_text);
                std::string cline;
                while (std::getline(iss, cline)) {
                    std::string up = uppercase(cline);
                    size_t boot_pos = up.find("BOOT");
                    if (boot_pos != std::string::npos) {
                        size_t colon = cline.find(':', boot_pos);
                        if (colon != std::string::npos) {
                            std::string token = cline.substr(colon + 1);
                            size_t slash = token.find_last_of("/\\");
                            if (slash != std::string::npos) token = token.substr(slash + 1);
                            size_t semi = token.find(';');
                            if (semi != std::string::npos) token.resize(semi);
                            while (!token.empty() && std::isspace(static_cast<unsigned char>(token.front()))) token.erase(0, 1);
                            while (!token.empty() && std::isspace(static_cast<unsigned char>(token.back()))) token.pop_back();
                            impl_->detected_ser = uppercase(token);
                            break;
                        }
                    }
                }
            }
            break;
        }
    }

    return true;
}

void DiscExtractor::close() {
    if (impl_->file.is_open()) {
        impl_->file.close();
    }
    impl_->sector_size    = RAW_SECTOR_SIZE;
    impl_->payload_offset = FORM1_OFFSET_2352;
    impl_->root_lba       = 0;
    impl_->root_size      = 0;
    impl_->vol_id.clear();
    impl_->detected_ser.clear();
}

bool DiscExtractor::is_open() const {
    return impl_->file.is_open();
}

std::string DiscExtractor::volume_id() const {
    return impl_->vol_id;
}

std::string DiscExtractor::detected_serial() const {
    return impl_->detected_ser;
}

uint32_t DiscExtractor::sector_size() const {
    return impl_->sector_size;
}

std::vector<DiscFileInfo> DiscExtractor::list_root() const {
    if (!impl_->file.is_open()) return {};
    return impl_->read_directory(impl_->root_lba, impl_->root_size);
}

std::vector<DiscFileInfo> DiscExtractor::list_directory(const std::string& dir_name) const {
    if (!impl_->file.is_open()) return {};
    std::string up_target = uppercase(dir_name);
    auto root_items = list_root();
    for (const auto& item : root_items) {
        if (item.is_directory && uppercase(item.path) == up_target) {
            return impl_->read_directory(item.lba, item.size);
        }
    }
    return {};
}

bool DiscExtractor::extract_file_by_lba(uint32_t lba, uint32_t size,
                                        const fs::path& dest_path,
                                        ProgressFn progress) {
    if (!impl_->file.is_open()) return false;

    fs::create_directories(dest_path.parent_path());
    std::ofstream out(dest_path, std::ios::binary);
    if (!out.is_open()) return false;

    uint32_t sectors = (size + FORM1_PAYLOAD_SZ - 1) / FORM1_PAYLOAD_SZ;
    uint32_t remaining = size;
    uint8_t  buf[FORM1_PAYLOAD_SZ];

    for (uint32_t i = 0; i < sectors; i++) {
        uint64_t offset = (static_cast<uint64_t>(lba + i) * impl_->sector_size) + impl_->payload_offset;
        impl_->file.clear();
        impl_->file.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
        if (!impl_->file.good()) return false;

        impl_->file.read(reinterpret_cast<char*>(buf), FORM1_PAYLOAD_SZ);
        if (static_cast<size_t>(impl_->file.gcount()) != FORM1_PAYLOAD_SZ) return false;

        uint32_t take = (remaining < FORM1_PAYLOAD_SZ) ? remaining : FORM1_PAYLOAD_SZ;
        out.write(reinterpret_cast<const char*>(buf), take);
        remaining -= take;

        if (progress && (i % 16 == 0 || i == sectors - 1)) {
            float frac = static_cast<float>(i + 1) / static_cast<float>(sectors);
            progress(frac, dest_path.filename().string());
        }
    }

    return true;
}

bool DiscExtractor::extract_primary_exe(const fs::path& dest_path, ProgressFn progress) {
    auto root_items = list_root();
    const DiscFileInfo* match = nullptr;

    // 1. Check for SLUS_005.84 or exact detected serial
    for (const auto& item : root_items) {
        if (!item.is_directory) {
            std::string up = uppercase(item.path);
            if (up == "SLUS_005.84" || (!impl_->detected_ser.empty() && up == impl_->detected_ser)) {
                match = &item;
                break;
            }
        }
    }

    // 2. Fallback: match any SLUS* or *.*84
    if (!match) {
        for (const auto& item : root_items) {
            if (!item.is_directory) {
                std::string up = uppercase(item.path);
                if (up.find("SLUS") != std::string::npos || up.find(".84") != std::string::npos) {
                    match = &item;
                    break;
                }
            }
        }
    }

    if (!match) return false;
    return extract_file_by_lba(match->lba, match->size, dest_path, progress);
}

int DiscExtractor::extract_directory_files(const std::string& iso_dir,
                                           const fs::path& dest_dir,
                                           ProgressFn progress) {
    auto items = list_directory(iso_dir);
    if (items.empty()) return 0;

    fs::create_directories(dest_dir);
    int extracted = 0;
    size_t total = items.size();

    for (size_t i = 0; i < total; i++) {
        if (!items[i].is_directory) {
            fs::path target = dest_dir / items[i].path;
            if (extract_file_by_lba(items[i].lba, items[i].size, target, nullptr)) {
                extracted++;
            }
            if (progress && (i % 4 == 0 || i == total - 1)) {
                float frac = static_cast<float>(i + 1) / static_cast<float>(total);
                progress(frac, items[i].path);
            }
        }
    }
    return extracted;
}

} // namespace psx_launcher
