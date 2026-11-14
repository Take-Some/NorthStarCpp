// Test service for a real project namespace and a plugin-only namespace.
#include <northstarcpp/script_service_plugin.h>

#include <cstring>
#include <new>

namespace {
struct Context {
    NorthStarScriptHostCall fallback;
    void* user;
};

void* create(NorthStarScriptHostCall fallback, void* user)
{
    return new (std::nothrow) Context{fallback, user};
}

void destroy(void* context) { delete static_cast<Context*>(context); }

int supports(void*, char const* method)
{
    return std::strcmp(method, "objectCount") == 0 || std::strcmp(method, "fail") == 0 ||
           std::strcmp(method, "crossNamespace") == 0 || std::strcmp(method, "answer") == 0;
}

int response(NorthStarScriptResult* result, char const* text, int success)
{
    result->json_utf8 = text;
    result->json_bytes = static_cast<uint32_t>(std::strlen(text));
    return success;
}

int invoke(void* raw, char const* method, char const* args, uint32_t bytes,
           NorthStarScriptResult* result)
{
    auto const& context = *static_cast<Context*>(raw);
    if (std::strcmp(method, "fail") == 0)
        return response(result, "Fixture service error", 0);
    if (std::strcmp(method, "answer") == 0)
        return response(result, "42", 1);
    char const* const scope = std::strcmp(method, "crossNamespace") == 0 ? "engine.logger" : "engine.scene";
    char const* const target = std::strcmp(method, "objectCount") == 0 ? method : "info";
    if (!context.fallback(context.user, scope, target, args, bytes, &result->json_utf8, &result->json_bytes))
        return 0;
    if (result->json_bytes != 1 || *result->json_utf8 != '7')
        return response(result, "Wrong host fallback value", 0);
    return response(result, "73", 1);
}

NorthStarScriptServiceApi const api{create, destroy, supports, invoke};
char const* last_error() { return ""; }
NorthStarModule const modules[]{
    {sizeof(NorthStarModule), NORTHSTARCPP_MODULE_ABI, "engine.scene", "Fixture scene service",
        "1", NORTHSTARCPP_SCRIPT_SERVICE_ABI, sizeof(api), &api, last_error},
    {sizeof(NorthStarModule), NORTHSTARCPP_MODULE_ABI, "engine.fixture", "Fixture extension service",
        "1", NORTHSTARCPP_SCRIPT_SERVICE_ABI, sizeof(api), &api, last_error},
};
}

extern "C" NORTHSTARCPP_MODULE_EXPORT const NorthStarModule* northstar_module_query(
    uint32_t host, char const* capability, uint32_t version)
{
    if (host == NORTHSTARCPP_MODULE_ABI && capability && version == NORTHSTARCPP_SCRIPT_SERVICE_ABI)
        for (auto const& module : modules)
            if (std::strcmp(capability, module.capability) == 0)
                return &module;
    return nullptr;
}
