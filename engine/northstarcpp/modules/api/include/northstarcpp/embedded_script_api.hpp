// NorthStarCpp: compile-time, immutable engine-owned JavaScript resources.
// This API is shared by the native Player and the Asset Browser.
#pragma once
#include <cstddef>
#include <string_view>

namespace northstarcpp::embedded_api {
struct Script {
    std::string_view virtual_path; // scripts/<category>/<name>.js, or .d.ts
    std::string_view source;       // original UTF-8 bytes; never writable
};
Script const* entries() noexcept;
std::size_t count() noexcept;
// Rejects unlisted paths; no disk fallback and no project VFS overlay.
Script const* find(std::string_view virtual_path) noexcept;
} // namespace northstarcpp::embedded_api
