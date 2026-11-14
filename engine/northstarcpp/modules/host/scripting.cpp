// NorthStarCpp engine.scripting: provider-neutral host bridge.
#include <northstarcpp/scripting.hpp>
#include <northstarcpp/assets.hpp>
#include <northstarcpp/shared_assets.hpp>
#include "module_loader.hpp"
#include <fstream>
#include <iterator>
#include <limits>
#include <map>
#include <utility>
#include <stdexcept>
#include <vector>

namespace northstarcpp {
namespace {
bool valid_identifier(std::string_view name, bool dots);
constexpr std::size_t max_host_bytes = NORTHSTAR_SCRIPTING_MAX_SOURCE_BYTES * 4ull;
}

struct ScriptRuntime::State {
    struct Service {
        State& runtime;
        std::string capability;
        Module module;
        NorthStarScriptServiceApi const* api;
        void* context = nullptr;

        Service(State& runtime, std::string capability)
            : runtime(runtime), capability(std::move(capability)), module(this->capability),
              api(module.query_optional<NorthStarScriptServiceApi>(NORTHSTARCPP_SCRIPT_SERVICE_ABI))
        {
            if (api) {
                context = api->create(&Service::fallback, this);
                if (!context)
                    throw module.error("Cannot create engine script service");
            }
        }

        ~Service()
        {
            if (context)
                api->destroy(context);
        }

        static int fallback(void* user, const char* capability, const char* method,
            const char* args, std::uint32_t length,
            const char** response, std::uint32_t* bytes) noexcept
        {
            auto& self = *static_cast<Service*>(user);
            if (!capability || self.capability != capability) {
                static constexpr char error[] = "Engine script fallback is scoped to its namespace";
                if (response) *response = error;
                if (bytes) *bytes = sizeof(error) - 1;
                return 0;
            }
            return self.runtime.handle_host_call(capability, method, args, length, response, bytes, false);
        }
    };

    Module module{NORTHSTARCPP_CAPABILITY_SCRIPTING};
    NorthStarScriptingApi const& api =
        *module.query<NorthStarScriptingApi>(NORTHSTARCPP_SCRIPTING_ABI);
    void* context = nullptr;
    ScriptingOptions options{};
    std::map<std::pair<std::string,std::string>, ScriptRuntime::ScriptHandler> bindings;
    std::string callback_output;
    std::string fallback_output;
    std::map<std::string, std::unique_ptr<Service>, std::less<>> services;

    static int dispatch(void* user, const char* capability, const char* method,
                        const char* args, std::uint32_t length,
                        const char** response, std::uint32_t* bytes) noexcept
    {
        return static_cast<State*>(user)->handle_host_call(
            capability, method, args, length, response, bytes, true);
    }

    std::string invoke_host(char const* capability, char const* method,
                           std::string_view args)
    {
        auto const it = bindings.find({capability, method});
        if (it == bindings.end())
            throw std::runtime_error(std::string("Engine API not registered: ") +
                capability + "." + method);
        auto const handler = it->second;
        return handler(args);
    }

    std::string invoke_service(char const* capability, char const* method,
                              char const* args, std::uint32_t length)
    {
        auto it = services.find(capability);
        if (it == services.end()) {
            if (services.size() >= 256)
                throw std::runtime_error("Too many engine script namespaces requested");
            auto service = std::make_unique<Service>(*this, capability);
            it = services.emplace(capability, std::move(service)).first;
        }
        auto const& service = *it->second;
        if (!service.api || !service.api->supports(service.context, method))
            return invoke_host(capability, method, {args, length});

        NorthStarScriptResult result{sizeof(NorthStarScriptResult), nullptr, 0};
        auto const ok = service.api->invoke(service.context, method, args, length, &result);
        bool const valid = result.size >= sizeof(result) &&
            result.json_utf8 && result.json_bytes && result.json_bytes <= max_host_bytes;
        if (!ok) {
            if (valid)
                throw std::runtime_error(std::string(result.json_utf8, result.json_bytes));
            throw service.module.error("Engine script service call failed");
        }
        if (!valid)
            throw service.module.error("Invalid engine script service response");
        return {result.json_utf8, result.json_bytes};
    }

    int handle_host_call(char const* capability, char const* method,
                         char const* args, std::uint32_t length,
                         char const** response, std::uint32_t* bytes, bool use_services) noexcept
    {
        auto& output = use_services ? callback_output : fallback_output;
        try {
            if (!capability || !method || !args || !response || !bytes ||
                length > max_host_bytes ||
                std::string_view(capability).rfind("engine.", 0) != 0 ||
                !valid_identifier(capability, true) || !valid_identifier(method, false))
                throw std::invalid_argument("Malformed scripting host call");
            output = use_services ? invoke_service(capability, method, args, length)
                                  : invoke_host(capability, method, {args, length});
            if (output.empty() || output.size() > max_host_bytes)
                throw std::runtime_error("Invalid engine API response size");
            *response = output.data();
            *bytes = static_cast<std::uint32_t>(output.size());
            return 1;
        } catch (std::exception const& e) {
            output = e.what();
        } catch (...) {
            output = "Unknown native engine API error";
        }
        if (response) *response = output.data();
        if (bytes) *bytes = static_cast<std::uint32_t>(output.size());
        return 0;
    }


    explicit State(ScriptingOptions opt) : options(opt) {
        if (opt.heap_limit_mb < 16 || opt.heap_limit_mb > 512 ||
            opt.timeout_ms == 0 || opt.timeout_ms > 60000 ||
            opt.max_source_bytes == 0 ||
            opt.max_source_bytes > NORTHSTAR_SCRIPTING_MAX_SOURCE_BYTES)
            throw std::invalid_argument("Invalid scripting options");
        NorthStarScriptingConfig config{sizeof(NorthStarScriptingConfig),
            opt.heap_limit_mb, opt.timeout_ms, opt.max_source_bytes};
        context = api.create(&config);
        if (!context)
            throw module.error("Cannot create scripting context");
        if (!api.set_host_call(context, &State::dispatch, this)) {
            api.destroy(context);
            context = nullptr;
            throw module.error("Cannot bind engine scripting host API");
        }
    }
    ~State() noexcept
    {
        services.clear();
        if (context)
            api.destroy(context);
    }
};

ScriptRuntime::ScriptRuntime(ScriptingOptions opt)
    : state_(std::make_unique<State>(opt)) {}
ScriptRuntime::~ScriptRuntime() = default;

std::string ScriptRuntime::provider_name() const { return state_->module.provider(); }
std::string ScriptRuntime::provider_version() const { return state_->module.version(); }

std::string ScriptRuntime::evaluate(std::string_view source, std::string_view origin) {
    if (source.size() > state_->options.max_source_bytes ||
        source.size() > std::numeric_limits<std::uint32_t>::max() ||
        source.find('\0') != std::string_view::npos)
        throw std::invalid_argument("JavaScript source is invalid or exceeds limit");
    if (origin.size() > 1024 || origin.find('\0') != std::string_view::npos)
        throw std::invalid_argument("Invalid scripting origin");
    // origin_utf8 must be NUL-terminated at the C ABI boundary.
    std::string file_name(origin);
    NorthStarScriptResult result{sizeof(NorthStarScriptResult), nullptr, 0};
    if (!state_->api.evaluate(state_->context, source.data(),
            static_cast<std::uint32_t>(source.size()), file_name.c_str(), &result))
        throw state_->module.error("JavaScript evaluation failed");
    if (result.size < sizeof(result) || !result.json_utf8 ||
        result.json_bytes > NORTHSTAR_SCRIPTING_MAX_SOURCE_BYTES * 4u)
        throw state_->module.error("Invalid scripting result");
    return std::string(result.json_utf8, result.json_bytes);
}

std::string ScriptRuntime::evaluate_file(std::filesystem::path const& file) {
    std::ifstream in(file, std::ios::binary | std::ios::ate);
    if (!in) throw std::runtime_error("Cannot open script: " + file.u8string());
    auto size = in.tellg();
    if (size < 0 || static_cast<std::uint64_t>(size) > state_->options.max_source_bytes)
        throw std::invalid_argument("JavaScript file exceeds source limit");
    std::string source(static_cast<std::size_t>(size), '\0');
    in.seekg(0);
    if (!source.empty() && !in.read(source.data(), static_cast<std::streamsize>(source.size())))
        throw std::runtime_error("Cannot read script: " + file.u8string());
    return evaluate(source, file.u8string());
}

std::string ScriptRuntime::evaluate_asset(AssetStore const& assets,
                                         std::string const& path) {
    auto const extension = std::filesystem::u8path(path).extension().u8string();
    if (extension != ".js" && extension != ".mjs")
        throw std::invalid_argument("Only JavaScript .js/.mjs assets may be evaluated");
    return evaluate(shared_assets::load_source(assets, path), path);
}

namespace {
bool valid_identifier(std::string_view name, bool dots) {
    if (name.empty() || name.size() > 96) return false;
    for (unsigned char ch : name)
        if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
              (ch >= '0' && ch <= '9') || ch == '_' || (dots && ch == '.')))
            return false;
    return true;
}
std::string quote_json(std::string_view value) {
    constexpr char hex[] = "0123456789abcdef";
    std::string out(1, '"');
    for (unsigned char ch : value) {
        if (ch == '"' || ch == '\\') { out += '\\'; out += static_cast<char>(ch); }
        else if (ch < 0x20) {
            out += "\\u00";
            out += hex[ch >> 4];
            out += hex[ch & 15];
        } else out += static_cast<char>(ch);
    }
    out += '"';
    return out;
}
} // namespace

void ScriptRuntime::bind(std::string capability, std::string method, ScriptHandler handler) {
    if (!valid_identifier(capability,true) || capability.rfind("engine.",0) != 0 ||
        !valid_identifier(method,false) || !handler)
        throw std::invalid_argument("Invalid engine API binding");
    state_->bindings[{std::move(capability),std::move(method)}] = std::move(handler);
}
void ScriptRuntime::unbind(std::string_view capability, std::string_view method) {
    state_->bindings.erase({std::string(capability),std::string(method)});
}
std::string ScriptRuntime::emit_event(std::string_view event, std::string_view payload_json) {
    if (!valid_identifier(event,true) ||
        payload_json.size() > state_->options.max_source_bytes)
        throw std::invalid_argument("Invalid engine scripting event");
    return evaluate("engine.emit(" + quote_json(event) + ",JSON.parse(" +
        quote_json(payload_json) + "))", "<engine-event>");
}

void ScriptRuntime::reset() {
    if (!state_->api.reset(state_->context))
        throw state_->module.error("Cannot reset scripting context");
    state_->services.clear();
}
} // namespace northstarcpp
