// NorthStarCpp
// Author: Kayla Verner
// Updated: 2026

#include "module_loader.hpp"
#include <northstarcpp/scene_component_registry.hpp>
#include <northstarcpp/provider_identity.hpp>
#include <boost/property_tree/json_parser.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace northstarcpp {
namespace {
namespace fs = std::filesystem;
using Tree = boost::property_tree::ptree;

std::string lowercase(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

fs::path environment_path(std::string const& variable)
{
#ifdef _WIN32
    std::wstring const name(variable.begin(), variable.end());
    std::array<wchar_t, 32768> buffer{};
    auto const size = GetEnvironmentVariableW(name.c_str(), buffer.data(),
        static_cast<DWORD>(buffer.size()));
    if (size >= buffer.size())
        throw std::runtime_error("NorthStarCpp module override path is too long");
    return size ? fs::path(buffer.data()) : fs::path{};
#else
    auto const* value = std::getenv(variable.c_str());
    return value ? fs::path(value) : fs::path{};
#endif
}

fs::path executable_directory()
{
#ifdef _WIN32
    std::array<wchar_t, 32768> path{};
    auto const length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (!length || length >= path.size())
        throw std::runtime_error("Cannot locate NorthStarCpp executable");
    auto const directory=fs::path(path.data()).parent_path();
    // Private Qt processes under bin/qt_tools still load providers and
    // bootstrap configuration from the parent bin directory.
    if(directory.filename()==L"qt_tools" &&
       fs::is_regular_file(directory.parent_path()/"bootstrap.json"))
        return directory.parent_path();
    return directory;
#else
    return fs::read_symlink("/proc/self/exe").parent_path();
#endif
}

Tree read_bootstrap(fs::path const& executable)
{
    fs::path const bootstrap = executable / "bootstrap.json";
    if (!fs::exists(bootstrap))
        return {};
    if (!fs::is_regular_file(bootstrap))
        throw std::runtime_error("bootstrap.json is not a regular file: " + bootstrap.u8string());
    if (fs::file_size(bootstrap) > 65536)
        throw std::runtime_error("bootstrap.json is larger than 64 KiB");

    Tree tree;
    try {
        std::ifstream stream(bootstrap, std::ios::binary);
        if (!stream)
            throw std::runtime_error("Cannot open file");
        boost::property_tree::read_json(stream, tree);
    } catch (std::exception const& ex) {
        throw std::runtime_error("Invalid bootstrap.json: " + std::string(ex.what()));
    }
    return tree;
}

fs::path resolve_path(fs::path const& base, std::string const& configured)
{
    auto const chosen = fs::u8path(configured);
    return (chosen.is_absolute() ? chosen : base / chosen).lexically_normal();
}

fs::path plugin_directory(fs::path const& executable, Tree const& bootstrap)
{
    auto const configured = bootstrap.get_optional<std::string>("plugins.path");
    if (!configured)
        return executable;
    if (configured->empty())
        throw std::runtime_error("bootstrap.json plugins.path cannot be empty");
    auto const directory = resolve_path(executable, *configured);
    if (!fs::is_directory(directory))
        throw std::runtime_error("Plugin directory from bootstrap.json does not exist: " + directory.u8string());
    return directory;
}

fs::path configured_override(Tree const& bootstrap, std::string const& capability, bool script)
{
    auto const overrides = bootstrap.get_child_optional(
        script ? "plugins.scriptOverrides" : "plugins.overrides");
    if (!overrides)
        return {};
    auto const item = overrides->find(capability);
    if (item == overrides->not_found())
        return {};
    if (!item->second.empty() || item->second.data().empty() || item->second.data() == "null")
        throw std::runtime_error("Invalid bootstrap.json plugin override for " + capability);
    return fs::u8path(item->second.data());
}

std::string environment_variable(std::string const& capability, bool script)
{
    std::string name = capability.substr(sizeof("engine.") - 1);
    for (auto& ch : name)
        ch = ch == '.' ? '_' : static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    return "NORTHSTARCPP_" + name + (script ? "_SCRIPT_PLUGIN" : "_PLUGIN");
}

void* load_module(fs::path const& filename)
{
#ifdef _WIN32
    return LoadLibraryExW(filename.c_str(), nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR |
        LOAD_LIBRARY_SEARCH_APPLICATION_DIR |
        LOAD_LIBRARY_SEARCH_SYSTEM32);
#else
    return dlopen(filename.c_str(), RTLD_NOW | RTLD_LOCAL);
#endif
}

std::string library_error()
{
#ifdef _WIN32
    return std::system_category().message(static_cast<int>(GetLastError()));
#else
    auto const* message = dlerror();
    return message ? message : "Unknown dynamic library error";
#endif
}

struct CloseModule {
    void operator()(void* handle) const noexcept
    {
#ifdef _WIN32
        FreeLibrary(static_cast<HMODULE>(handle));
#else
        dlclose(handle);
#endif
    }
};

NorthStarModule const* module_descriptor(NorthStarModuleQuery query, char const* capability,
    uint32_t api_version, uint32_t api_size, bool (*valid)(void const*))
{
    auto const* candidate = query ? query(NORTHSTARCPP_MODULE_ABI, capability, api_version) : nullptr;
    if (!candidate || candidate->size < sizeof(NorthStarModule) ||
        candidate->host_abi != NORTHSTARCPP_MODULE_ABI ||
        !candidate->capability || std::strcmp(candidate->capability, capability) != 0 ||
        candidate->api_version != api_version || candidate->api_size < api_size ||
        !candidate->api || !candidate->last_error ||
        !candidate->provider || !*candidate->provider ||
        !candidate->version || !*candidate->version ||
        !valid(candidate->api))
        return nullptr;
    return candidate;
}

std::vector<fs::path> external_candidates(fs::path const& directory)
{
    std::vector<fs::path> candidates;
    // One flat search directory for every capability.
    for (auto const& entry : fs::directory_iterator(directory, fs::directory_options::skip_permission_denied)) {
        auto const extension = entry.path().extension().u8string();
#ifdef _WIN32
        if (lowercase(extension) != ".dll")
#else
        if (extension != ".so")
#endif
            continue;
        std::error_code status;
        if (entry.is_regular_file(status) && !status)
            candidates.push_back(entry.path());
    }
    std::sort(candidates.begin(), candidates.end());
    return candidates;
}

} // namespace

Module::Module(std::string capability, std::string preferred_provider)
    : capability_id(std::move(capability)), preferred_provider_id(lowercase(std::move(preferred_provider)))
{
    constexpr std::string_view prefix = "engine.";
    if (capability_id.compare(0, prefix.size(), prefix) != 0 ||
        capability_id.size() <= prefix.size() || capability_id.size() > 96 ||
        capability_id.back() == '.' || capability_id.find("..") != std::string::npos ||
        capability_id.find_first_not_of(
            "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.", prefix.size()) !=
                std::string::npos)
        throw std::invalid_argument("Invalid engine capability: " + capability_id);
}

Module::~Module()
{
    if (handle)
        release_scene_components_for_module(handle);
        CloseModule{}(handle);
}

void* Module::find_symbol(void* library, char const* name)
{
    if (!library)
        return nullptr;
#ifdef _WIN32
    return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(library), name));
#else
    return dlsym(library, name);
#endif
}

void const* Module::resolve(uint32_t api_version, uint32_t api_size, Validator valid, Mode mode)
{
    auto match = [&](NorthStarModuleQuery query) {
        return module_descriptor(query, capability_id.c_str(), api_version, api_size, valid);
    };
    if (descriptor || handle) {
        auto const* module = match(selected_query);
        if (module || mode == Mode::optional || (!selected_query && mode == Mode::legacy))
            return module ? module->api : nullptr;
        throw error("Selected module does not provide the requested ABI");
    }

    bool const script = api_version == NORTHSTARCPP_SCRIPT_SERVICE_ABI;
    auto provider_variable=environment_variable(capability_id,false);
    provider_variable.replace(provider_variable.size()-7,7,"_PROVIDER");
    auto const preferred = !script && preferred_provider_id.empty()
        ? lowercase(environment_path(provider_variable).u8string()) : std::string{};
    auto const explicit_choice=preferred_provider_id.empty()?
        preferred:preferred_provider_id;
    auto matching_choice=[&](NorthStarModule const* module) {
        if(!module)return false;
        if(explicit_choice.empty())return true;
        return canonical_provider_id(module->provider,capability_id)==explicit_choice;
    };
    auto requested = environment_path(environment_variable(capability_id, script));
    fs::path executable;
    Tree bootstrap;
    if (requested.empty()) {
        executable = executable_directory();
        bootstrap = read_bootstrap(executable);
        requested = configured_override(bootstrap, capability_id, script);
        if (!requested.empty() && !requested.is_absolute())
            requested = executable / requested;
    }

    if (!requested.empty()) {
        path = fs::absolute(requested).lexically_normal();
        std::unique_ptr<void, CloseModule> library(load_module(path));
        if (!library) {
            auto const reason = library_error();
            throw std::runtime_error("Cannot load " + capability_id + " from " + path.u8string() +
                ": " + reason);
        }
        auto const query = reinterpret_cast<NorthStarModuleQuery>(
            find_symbol(library.get(), "northstar_module_query"));
        auto const* module = match(query);
        if(module && !matching_choice(module))
            throw std::runtime_error("Plugin path override conflicts with selected provider '"+
                explicit_choice+"' for "+capability_id+": "+path.u8string());
        if(!query && mode==Mode::legacy && !explicit_choice.empty())
            throw std::runtime_error("Legacy module cannot confirm selected provider for "+
                capability_id);
        if (module || (!query && mode == Mode::legacy)) {
            auto registrar=reinterpret_cast<NorthStarSceneComponentsRegisterC>(
                find_symbol(library.get(),"northstarcpp_register_scene_components_v1"));
            if(registrar && !acquire_scene_components_for_module(library.get(),registrar))
                throw std::runtime_error("Scene component DLL registration failed for "+
                    capability_id+": "+path.u8string());
            descriptor = module;
            selected_query = query;
            handle = library.release();
            return module ? module->api : nullptr;
        }
        throw std::runtime_error("Incompatible module for " + capability_id +
            " (ABI " + std::to_string(api_version) + "): " + path.u8string());
    }

    auto const directory = plugin_directory(executable, bootstrap);
    // Per-process project preferences are applied to all engine.* capabilities
    // by the Project Browser. A direct Module(preferred_provider) request is
    // authoritative, so diagnostics may still inspect another implementation.
    auto accepts = [&](NorthStarModule const* module) {
        if(!module)return false;
        auto const id=canonical_provider_id(module->provider,capability_id);
        if(!preferred.empty()&&id!=preferred)return false;
        return preferred_provider_id.empty()||id==preferred_provider_id;
    };
    for (auto const& candidate : external_candidates(directory)) {
        std::unique_ptr<void, CloseModule> library(load_module(candidate));
        if (!library)
            continue;
        auto const query = reinterpret_cast<NorthStarModuleQuery>(
            find_symbol(library.get(), "northstar_module_query"));
        auto const* module = match(query);
        if (accepts(module)) {
            auto registrar=reinterpret_cast<NorthStarSceneComponentsRegisterC>(
                find_symbol(library.get(),"northstarcpp_register_scene_components_v1"));
            if(registrar && !acquire_scene_components_for_module(library.get(),registrar))
                throw std::runtime_error("Scene component DLL registration failed for "+
                    capability_id+": "+candidate.u8string());
            path = candidate;
            descriptor = module;
            selected_query = query;
            handle = library.release();
            return module->api;
        }
    }

    for (auto entry = builtin_modules; entry->query; ++entry) {
        if (capability_id != entry->capability)
            continue;
        auto const* module = match(entry->query);
        if (accepts(module)) {
            path.clear();
            descriptor = module;
            selected_query = entry->query;
            return module->api;
        }
    }
    if (mode == Mode::optional)
        return nullptr;
    throw std::runtime_error("No compatible module for " + capability_id +
        " (ABI " + std::to_string(api_version) + ") in " + directory.u8string() +
        (preferred.empty() ? "" : " (selected provider: " + preferred + ")"));
}

void const* Module::resolve_extension(uint32_t api_version, uint32_t api_size, Validator valid) const
{
    auto const* extension = module_descriptor(
        selected_query, capability_id.c_str(), api_version, api_size, valid);
    return extension ? extension->api : nullptr;
}

std::runtime_error Module::error(char const* action) const
{
    auto const* message = descriptor ? descriptor->last_error() : nullptr;
    return std::runtime_error(std::string(action) + " [" + capability_id + ", " +
        (path.empty() ? "builtin" : path.u8string()) + "]" +
        (message && *message ? std::string(": ") + message : ""));
}

std::string Module::provider() const { return descriptor ? descriptor->provider : ""; }
std::string Module::version() const { return descriptor ? descriptor->version : ""; }

std::string Module::source() const {
    return !descriptor ? std::string{} : (path.empty() ? "builtin" : path.u8string());
}
bool Module::is_external() const noexcept {
    return descriptor && handle != nullptr;
}

} // namespace northstarcpp
