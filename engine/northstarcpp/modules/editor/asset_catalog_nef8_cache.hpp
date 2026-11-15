// Read-only NEF8 dictionary metadata cache shared between project/engine
// scans. A dictionary is decoded only if its disk metadata has changed.
#pragma once
#include <northstarcpp/shared_assets.hpp>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>
namespace northstarcpp::editor::detail {
struct IndexedDictionary {
    std::shared_ptr<const std::vector<shared_assets::DictionaryEntry>> entries;
    bool cache_hit=false;
};
IndexedDictionary inspect_dictionary(std::filesystem::path const& source,
                                     std::string const& relative,
                                     std::uintmax_t byte_size);
} // namespace northstarcpp::editor::detail
