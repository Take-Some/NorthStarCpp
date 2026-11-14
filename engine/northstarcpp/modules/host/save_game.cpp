// NorthStarCpp - bounded, atomic, per-project gameplay saves.
// Copyright (C) 2026 Kayla's Systems - Kayla Verner.
#include <northstarcpp/save_game.hpp>
#include <boost/property_tree/json_parser.hpp>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include <cstdint>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <system_error>

namespace northstarcpp {
namespace {
namespace fs = std::filesystem;
constexpr char header[] = "NorthStarCpp.Save 1\n";
constexpr std::size_t max_disk_bytes = SaveGameStore::max_payload_bytes + 128u;

void validate_json(std::string_view json) {
    if (json.empty() || json.size() > SaveGameStore::max_payload_bytes ||
        json.find('\0') != std::string_view::npos)
        throw std::invalid_argument("Save payload is empty, oversized or contains NUL");
    auto const first = json.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos || (json[first] != '{' && json[first] != '['))
        throw std::invalid_argument("Save payload must be a JSON object or array");
    try {
        boost::property_tree::ptree value;
        std::istringstream input{std::string(json)};
        boost::property_tree::read_json(input, value);
        input >> std::ws;
        if (input.peek() != std::char_traits<char>::eof())
            throw std::invalid_argument("Trailing data in JSON save payload");
    } catch (boost::property_tree::json_parser_error const&) {
        throw std::invalid_argument("Invalid JSON save payload");
    }
}

std::uint64_t fingerprint(std::string_view text) noexcept {
    std::uint64_t hash = 14695981039346656037ull;
    for (unsigned char byte : text) {
        hash ^= byte;
        hash *= 1099511628211ull;
    }
    return hash;
}
std::string hash_text(std::string_view payload) {
    std::ostringstream out;
    out << std::hex << std::setw(16) << std::setfill('0') << fingerprint(payload);
    return out.str();
}
bool safe_slot(std::string_view slot) noexcept {
    if (slot.empty() || slot.size() > 48 ||
        !((slot[0] >= 'a' && slot[0] <= 'z') ||
          (slot[0] >= 'A' && slot[0] <= 'Z') ||
          (slot[0] >= '0' && slot[0] <= '9')))
        return false;
    for (unsigned char ch : slot)
        if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
              (ch >= '0' && ch <= '9') || ch == '-' || ch == '_'))
            return false;
    return true;
}
void reject_symlink(fs::path const& path) {
    std::error_code ec;
    auto const state = fs::symlink_status(path, ec);
    if (!ec && fs::is_symlink(state))
        throw std::runtime_error("Save path cannot be a symbolic link");
}
} // namespace

SaveGameStore::SaveGameStore(fs::path directory)
    : directory_(std::move(directory)) {
    if (directory_.empty() || !directory_.is_absolute())
        throw std::invalid_argument("Save directory must be absolute");
}

void SaveGameStore::check_directory() const {
    // The host chooses the root; scripts only provide a validated slot.
    reject_symlink(directory_);
    if (fs::exists(directory_) && !fs::is_directory(directory_))
        throw std::runtime_error("Save storage root is not a directory");
}

fs::path SaveGameStore::path_for(std::string_view slot) const {
    if (!safe_slot(slot)) throw std::invalid_argument(
        "Save slot must be 1-48 ASCII alphanumeric, hyphen or underscore");
    return directory_ / (std::string(slot) + ".nsave");
}

bool SaveGameStore::exists(std::string_view slot) const {
    check_directory();
    auto const path = path_for(slot);
    reject_symlink(path);
    return fs::is_regular_file(path);
}

void SaveGameStore::write(std::string_view slot, std::string_view json) {
    auto const destination = path_for(slot);
    check_directory();
    validate_json(json);
    fs::create_directories(directory_);
    check_directory();
    reject_symlink(destination);
#ifdef _WIN32
    // GetTempFileNameW creates a new file in the same directory, ensuring the
    // final replacement can be atomic and concurrent writers cannot reuse it.
    wchar_t temporary_name[MAX_PATH + 1]{};
    if (!GetTempFileNameW(directory_.c_str(), L"nsv", 0, temporary_name))
        throw std::runtime_error("Cannot create secure temporary save file");
    fs::path const temporary(temporary_name);
#else
    auto const temporary = directory_ / (std::string(slot) + ".nsave.tmp");
#endif
    try {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        if (!out) throw std::runtime_error("Cannot open temporary save file");
        out << header << json.size() << '\n' << hash_text(json) << '\n';
        out.write(json.data(), static_cast<std::streamsize>(json.size()));
        out.flush();
        if (!out) throw std::runtime_error("Cannot write complete save file");
        out.close();
        if (!out) throw std::runtime_error("Cannot close temporary save file");
#ifdef _WIN32
        if (!MoveFileExW(temporary.c_str(), destination.c_str(),
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("Atomic save replacement failed");
#else
        fs::rename(temporary, destination);
#endif
    } catch (...) {
        std::error_code ec;
        fs::remove(temporary, ec);
        throw;
    }
}

std::optional<std::string> SaveGameStore::read(std::string_view slot) const {
    auto const path = path_for(slot);
    check_directory();
    reject_symlink(path);
    if (!fs::exists(path)) return std::nullopt;
    if (!fs::is_regular_file(path) || fs::file_size(path) > max_disk_bytes)
        throw std::runtime_error("Save file is not regular or exceeds size limit");
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("Cannot open save file");
    std::string signature, length, hash;
    if (!std::getline(in, signature) || signature + '\n' != header ||
        !std::getline(in, length) || !std::getline(in, hash) ||
        length.empty() || length.size() > 10 || hash.size() != 16)
        throw std::runtime_error("Unsupported or corrupt save file header");
    for (unsigned char ch : length)
        if (ch < '0' || ch > '9')
            throw std::runtime_error("Invalid save length");
    auto const bytes = std::stoull(length);
    if (bytes > max_payload_bytes) throw std::runtime_error("Save payload is too large");
    std::string payload(static_cast<std::size_t>(bytes), '\0');
    if (!in.read(payload.data(), static_cast<std::streamsize>(payload.size())) ||
        in.peek() != std::char_traits<char>::eof() ||
        hash != hash_text(payload))
        throw std::runtime_error("Save file is truncated or checksum mismatched");
    validate_json(payload);
    return payload;
}

bool SaveGameStore::erase(std::string_view slot) {
    auto const path = path_for(slot);
    check_directory();
    reject_symlink(path);
    return fs::remove(path);
}

} // namespace northstarcpp
