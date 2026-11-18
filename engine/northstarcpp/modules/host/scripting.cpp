// NorthStarCpp engine.scripting: provider-neutral host bridge.
#include <northstarcpp/scripting.hpp>
#include <northstarcpp/assets.hpp>
#include <northstarcpp/shared_assets.hpp>
#include "module_loader.hpp"
#include "script_api_hierarchy.hpp"
#include <algorithm>
#include <iterator>
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
std::string quote_json(std::string_view value);
constexpr std::size_t max_host_bytes = NORTHSTAR_SCRIPTING_MAX_SOURCE_BYTES * 4ull;

// Import lowering for VFS-backed class exports. Both engine.scripting
// providers currently evaluate classic scripts, so these *named*, static
// imports are translated to lexical bindings in an isolated function scope.
// This intentionally does not claim full ESM (export, dynamic import, cycles).
std::string lower_engine_imports(std::string_view source) {
    auto const identifier_start=[](char c) {
        return (c>='A' && c<='Z') || (c>='a' && c<='z') ||
               c=='_' || c=='$';
    };
    auto const identifier_part=[&](char c) {
        return identifier_start(c) || (c>='0' && c<='9');
    };
    auto skip_whitespace=[&](std::size_t& i) {
        while(i<source.size() &&
              (source[i]==' ' || source[i]=='\t' || source[i]=='\n' || source[i]=='\r'))
            ++i;
    };
    auto skip_comments=[&](std::size_t& i) {
        for(;;) {
            skip_whitespace(i);
            if(source.substr(i,2)=="//") {
                i+=2;
                while(i<source.size() && source[i]!='\n') ++i;
            } else if(source.substr(i,2)=="/*") {
                auto const end=source.find("*/",i+2);
                if(end==std::string_view::npos)
                    throw std::invalid_argument("Unclosed script header comment");
                i=end+2;
            } else break;
        }
    };
    auto read_identifier=[&](std::size_t& i) {
        if(i>=source.size() || !identifier_start(source[i]))
            throw std::invalid_argument("Expected a named engine module identifier");
        auto const start=i++;
        while(i<source.size() && identifier_part(source[i])) ++i;
        return std::string(source.substr(start,i-start));
    };
    std::string result(source);
    std::string bindings;
    std::size_t cursor=0;
    std::size_t import_count=0;
    for(;;) {
        skip_comments(cursor);
        if(source.substr(cursor,6)!="import" ||
           cursor+6>=source.size() ||
           identifier_part(source[cursor+6]))
            break;
        auto const start=cursor;
        cursor+=6;
        skip_whitespace(cursor);
        if(cursor>=source.size() || source[cursor]!='{')
            throw std::invalid_argument(
                "Only named imports from @northstar/engine are currently supported");
        ++cursor;
        unsigned names=0;
        for(;;) {
            skip_whitespace(cursor);
            if(cursor>=source.size())
                throw std::invalid_argument("Unterminated named engine import");
            if(source[cursor]=='}') {
                ++cursor;
                if(!names) throw std::invalid_argument("Empty engine import");
                break;
            }
            auto const imported=read_identifier(cursor);
            // The SDK's EngineModuleRegistry is the single export registry.
            // Do not duplicate that allowlist in C++: new engine classes must
            // become importable when the class registry is updated.
            // Identifiers are already strictly validated by read_identifier.
            skip_whitespace(cursor);
            std::string local=imported;
            if(source.substr(cursor,2)=="as" &&
               cursor+2<source.size() &&
               !identifier_part(source[cursor+2])) {
                cursor+=2;
                skip_whitespace(cursor);
                local=read_identifier(cursor);
            }
            // Validate against the actual runtime registry. This also gives
            // useful diagnostics for typos and missing optional providers.
            // read_identifier accepts only safe ASCII JS identifier bytes.
            // Construct checked quotes without depending on later JSON helpers.
            bindings+="if (!(\""+imported+
                "\" in globalThis.NorthStar.modules.engine)) throw new TypeError(\"Unknown @northstar/engine export: "+
                imported+"\");\n";
            bindings+="const "+local+" = globalThis.NorthStar.modules.engine."+
                imported+";\n";
            ++names;
            if(names>32) throw std::invalid_argument("Too many engine imports");
            skip_whitespace(cursor);
            if(cursor<source.size() && source[cursor]==',') {
                ++cursor;
                skip_whitespace(cursor);
                if(cursor<source.size() && source[cursor]=='}') {
                    ++cursor;
                    break;
                }
                continue;
            }
            if(cursor>=source.size() || source[cursor]!='}')
                throw std::invalid_argument("Malformed named engine import");
            ++cursor;
            break;
        }
        skip_whitespace(cursor);
        if(source.substr(cursor,4)!="from" ||
           (cursor+4<source.size() && identifier_part(source[cursor+4])))
            throw std::invalid_argument("Expected 'from' in engine import");
        cursor+=4;
        skip_whitespace(cursor);
        if(cursor>=source.size() ||
           (source[cursor]!='"' && source[cursor]!='\''))
            throw std::invalid_argument("Expected quoted engine module specifier");
        char const quote=source[cursor++];
        auto const spec_start=cursor;
        while(cursor<source.size() && source[cursor]!=quote) {
            if(source[cursor]=='\\')
                throw std::invalid_argument("Escaped engine module specifiers are unsupported");
            ++cursor;
        }
        if(cursor>=source.size()) throw std::invalid_argument("Unterminated engine module specifier");
        auto const spec=source.substr(spec_start,cursor-spec_start);
        ++cursor;
        if(spec!="@northstar/engine")
            throw std::invalid_argument("Unsupported module specifier: "+std::string(spec));
        while(cursor<source.size() && (source[cursor]==' ' || source[cursor]=='\t')) ++cursor;
        if(cursor<source.size() && source[cursor]==';') ++cursor;
        if(cursor<source.size() && source[cursor]!='\r' && source[cursor]!='\n')
            throw std::invalid_argument("Unexpected characters after engine import");
        for(std::size_t j=start;j<cursor;++j)
            if(result[j]!='\r' && result[j]!='\n') result[j]=' ';
        if(++import_count>32) throw std::invalid_argument("Too many engine import statements");
    }
    if(!import_count) return std::string(source);
    return "(function(){\n\"use strict\";\n"+bindings+result+"\n})();";
}
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
        auto it = bindings.find({capability, method});
        if (it == bindings.end()) {
            const auto legacy=script_api_hierarchy::legacy_namespace(
                capability,method);
            if(!legacy.empty())
                it=bindings.find({std::string(legacy),method});
        }
        if (it == bindings.end()) {
            auto const target=script_api_hierarchy::canonical_method(
                capability,method);
            if(!target.first.empty())
                it=bindings.find({std::string(target.first),
                                  std::string(target.second)});
        }
        if (it == bindings.end())
            throw std::runtime_error(std::string("Engine API not registered: ") +
                capability + "." + method);
        auto const handler = it->second;
        return handler(args);
    }

    std::string invoke_service(char const* capability, char const* method,
                              char const* args, std::uint32_t length,
                              bool allow_alias=true)
    {
        auto it = services.find(capability);
        if (it == services.end()) {
            if (services.size() >= 256)
                throw std::runtime_error("Too many engine script namespaces requested");
            auto service = std::make_unique<Service>(*this, capability);
            it = services.emplace(capability, std::move(service)).first;
        }
        auto const& service = *it->second;
        if (!service.api || !service.api->supports(service.context, method)) {
            // Prefer explicitly registered nested native methods. Otherwise
            // delegate an alias to its original capability through the
            // normal service dispatch, so a legacy external plugin remains
            // authoritative even when called through the new API hierarchy.
            if (bindings.find({capability, method}) != bindings.end())
                return invoke_host(capability, method, {args, length});
            if(allow_alias) {
                auto const legacy=script_api_hierarchy::legacy_namespace(
                    capability,method);
                if(!legacy.empty()) {
                    const auto name=std::string(legacy);
                    return invoke_service(name.c_str(),method,args,length,false);
                }
                auto const target=script_api_hierarchy::canonical_method(
                    capability,method);
                if(!target.first.empty()) {
                    const auto name=std::string(target.first);
                    const auto operation=std::string(target.second);
                    return invoke_service(name.c_str(),operation.c_str(),
                                          args,length,false);
                }
            }
            return invoke_host(capability, method, {args, length});
        }

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
    auto const compiled=lower_engine_imports(source);
    if(compiled.size()>state_->options.max_source_bytes)
        throw std::length_error("Imported script exceeds JavaScript source limit");
    NorthStarScriptResult result{sizeof(NorthStarScriptResult), nullptr, 0};
    if (!state_->api.evaluate(state_->context, compiled.data(),
            static_cast<std::uint32_t>(compiled.size()), file_name.c_str(), &result))
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
