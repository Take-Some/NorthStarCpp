// NorthStarCpp provider-neutral engine.scripting C++ SDK.
#pragma once
#include <northstarcpp/scripting_plugin.h>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace northstarcpp {
class AssetStore;

struct ScriptingOptions {
    std::uint32_t heap_limit_mb{64};
    std::uint32_t timeout_ms{1000};
    std::uint32_t max_source_bytes{NORTHSTAR_SCRIPTING_MAX_SOURCE_BYTES};
};

// Each ScriptRuntime owns an independent persistent JavaScript context.
// Call a given instance from one thread at a time. V8 scripts have no
// automatic filesystem/network access. Engine calls use installed script
// services and explicitly bound host methods.
class ScriptRuntime final {
public:
    explicit ScriptRuntime(ScriptingOptions options = {});
    ~ScriptRuntime();
    ScriptRuntime(ScriptRuntime const&) = delete;
    ScriptRuntime& operator=(ScriptRuntime const&) = delete;
    ScriptRuntime(ScriptRuntime&&) = delete;
    ScriptRuntime& operator=(ScriptRuntime&&) = delete;

    std::string provider_name() const;
    std::string provider_version() const;
    // Evaluates JavaScript and returns the JSON-serialized completion value.
    std::string evaluate(std::string_view source, std::string_view origin = "<script>");
    std::string evaluate_file(std::filesystem::path const& file);
    std::string evaluate_asset(AssetStore const& assets, std::string const& path);
    // JSON arguments and return values. Installed services may override these
    // host methods. The scripting ABI exposes no native pointers to JavaScript.
    using ScriptHandler = std::function<std::string(std::string_view args_json)>;
    void bind(std::string capability, std::string method, ScriptHandler handler);
    void unbind(std::string_view capability, std::string_view method);
    // Emit an engine lifecycle/gameplay event to engine.on(event, callback).
    // payload_json must be a valid JSON value (e.g. "null", "0.016", "{}").
    std::string emit_event(std::string_view event, std::string_view payload_json = "null");
    void reset();

private:
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace northstarcpp
