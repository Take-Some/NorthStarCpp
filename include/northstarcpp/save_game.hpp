// NorthStarCpp - provider-neutral project save storage.
// Copyright (C) 2026 Kayla's Systems - Kayla Verner.
#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace northstarcpp {

// Stores JSON objects/arrays supplied by project scripts. Does not serialize
// physics worlds automatically; the project owns its gameplay state schema.
class SaveGameStore final {
public:
    static constexpr std::size_t max_payload_bytes = 256u * 1024u;

    explicit SaveGameStore(std::filesystem::path directory);

    void write(std::string_view slot, std::string_view json);
    std::optional<std::string> read(std::string_view slot) const;
    bool exists(std::string_view slot) const;
    bool erase(std::string_view slot);

private:
    std::filesystem::path path_for(std::string_view slot) const;
    void check_directory() const;
    std::filesystem::path directory_;
};

} // namespace northstarcpp
