// NorthStarCpp
// Author: Kayla Verner
// Updated: 2026

#pragma once
#include <northstarcpp/module.h>
#include "module_contracts.hpp"
#include <filesystem>
#include <stdexcept>
#include <string>

namespace northstarcpp {

struct BuiltinModule {
    char const* capability;
    NorthStarModuleQuery query;
};
extern BuiltinModule const builtin_modules[];

class Module {
public:
    explicit Module(std::string capability);
    ~Module();
    Module(Module const&) = delete;
    Module& operator=(Module const&) = delete;

    template<class Fn>
    Fn symbol(char const* name) const
    {
        return reinterpret_cast<Fn>(find_symbol(handle, name));
    }

    template<class Api>
    Api const* query(uint32_t api_version, bool allow_legacy = false)
    {
        return static_cast<Api const*>(resolve(api_version, sizeof(Api), &validate<Api>,
            allow_legacy ? Mode::legacy : Mode::required));
    }

    template<class Api>
    Api const* query_optional(uint32_t api_version)
    {
        return static_cast<Api const*>(resolve(api_version, sizeof(Api), &validate<Api>,
            Mode::optional));
    }

    template<class Api>
    Api const* query_extension(uint32_t api_version) const
    {
        return static_cast<Api const*>(resolve_extension(api_version, sizeof(Api), &validate<Api>));
    }

    std::runtime_error error(char const* action) const;
    std::string provider() const;
    std::string version() const;

private:
    enum class Mode { required, optional, legacy };
    using Validator = bool (*)(void const*);
    template<class Api>
    static bool validate(void const* table)
    {
        return valid_api(*static_cast<Api const*>(table));
    }

    static void* find_symbol(void* library, char const* name);
    void const* resolve(uint32_t api_version, uint32_t api_size, Validator valid, Mode mode);
    void const* resolve_extension(uint32_t api_version, uint32_t api_size, Validator valid) const;

    std::filesystem::path path;
    std::string capability_id;
    void* handle = nullptr;
    NorthStarModuleQuery selected_query = nullptr;
    NorthStarModule const* descriptor = nullptr;
};

} // namespace northstarcpp
