// Engine-wide provider resolution diagnostics. This does not initialize devices.
// Copyright (C) 2026 Kayla's Systems — Kayla Verner.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace northstarcpp {
// A resolution describes what a newly constructed Module would select at
// inspection time. Existing instances retain their chosen module until recreated.
struct ProviderResolution {
    std::string capability;
    std::string requested_provider;
    std::string provider;
    std::string version;
    std::string source;          // "builtin" or absolute external module path
    std::string error;           // Empty on successful ABI negotiation
    std::uint32_t api_abi = 0;
    bool available = false;
    bool external = false;
};

// Never invokes provider create() or loads a scene. Uses the real host module
// loader and its table validation; an explicit incompatible override is visible
// as an error rather than silently falling back to the builtin.
std::vector<ProviderResolution> inspect_engine_providers();
} // namespace northstarcpp
