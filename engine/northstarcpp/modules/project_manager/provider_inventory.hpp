// Live inventory for the Qt Project Browser.
// Copyright (C) 2026 Kayla's Systems — Kayla Verner.
#pragma once
#include <filesystem>
#include <cstdint>
#include <string>
#include <vector>

namespace northstarcpp::projects {
struct ProviderChoice {
    std::string capability; // engine.*
    std::string id;         // provider descriptor id
    std::string version;
    std::string source;     // builtin or verified external DLL
    bool builtin=false;
    bool recommended=false;
    // Extension tables actually published through northstar_module_query;
    // these are validated independently of the provider's feature claims.
    std::uint64_t verified_render_apis=0;
    // Compiled-feature declaration, not a GPU hardware/runtime acceptance.
    std::uint64_t advertised_render_features=0;
    bool has_feature_manifest=false;
};
std::vector<ProviderChoice> discover_provider_choices(
    std::filesystem::path const& executable_directory);
// Single source for child-process provider selection variable names.
std::string provider_environment_key(std::string const& capability);
} // namespace northstarcpp::projects
