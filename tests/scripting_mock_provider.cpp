// Test-only scripting provider: verifies the ABI without pretending to be V8.
#include <northstarcpp/scripting_plugin.h>
#include "module_provider.hpp"
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
using northstarcpp::module_provider::guarded;
using northstarcpp::module_provider::last_error;
using northstarcpp::module_provider::match;

struct MockContext {
    std::uint32_t counter = 0;
    std::uint32_t max_source = 0;
    std::string result = "null";
    NorthStarScriptHostCall host_call = nullptr;
    void* host_user = nullptr;
};
void require(bool value, char const* message) {
    if (!value) throw std::invalid_argument(message);
}
void* create(NorthStarScriptingConfig const* config) {
    MockContext* state = nullptr;
    guarded([&] {
        require(config && config->size >= sizeof(*config), "Invalid config");
        require(config->heap_limit_mb >= 16 && config->heap_limit_mb <= 512 &&
                config->timeout_ms > 0 && config->timeout_ms <= 60000 &&
                config->max_source_bytes > 0 &&
                config->max_source_bytes <= NORTHSTAR_SCRIPTING_MAX_SOURCE_BYTES,
                "Invalid config limits");
        state = new MockContext{0, config->max_source_bytes, "null"};
    });
    return state;
}
void destroy(void* context) { delete static_cast<MockContext*>(context); }
int evaluate(void* context, char const* source, std::uint32_t length,
             char const* /*origin*/, NorthStarScriptResult* output) {
    return guarded([&] {
        require(context && source && output && output->size >= sizeof(*output),
                "Invalid evaluation arguments");
        auto& state = *static_cast<MockContext*>(context);
        require(length <= state.max_source, "Source too large");
        auto command = std::string_view(source, length);
        if (command == "raise") throw std::runtime_error("Mock script exception");
        if (command.substr(0, 5) == "call ") {
            auto const separator = command.find(' ', 5);
            require(separator != std::string_view::npos && state.host_call, "Invalid mock host call");
            auto const capability = std::string(command.substr(5, separator - 5));
            auto const method = std::string(command.substr(separator + 1));
            char const* response = nullptr;
            std::uint32_t bytes = 0;
            if (!state.host_call(state.host_user, capability.c_str(), method.c_str(),
                                 "[]", 2, &response, &bytes))
                throw std::runtime_error(std::string(response, bytes));
            state.result.assign(response, bytes);
        } else if (command == "host") {
            require(state.host_call, "No host dispatcher");
            char const* response = nullptr;
            std::uint32_t bytes = 0;
            if (!state.host_call(state.host_user,"engine.mock","add","[4,5]",5,
                                 &response,&bytes))
                throw std::runtime_error("Mock engine API was rejected");
            state.result.assign(response,bytes);
        } else
        if (command == "1+2") state.result = "3";
        else if (command == "counter++") state.result = std::to_string(++state.counter);
        else state.result = "null";
        output->json_utf8 = state.result.data();
        output->json_bytes = static_cast<std::uint32_t>(state.result.size());
    });
}
int reset(void* context) {
    return guarded([&] {
        require(context, "Missing script context");
        auto& state = *static_cast<MockContext*>(context);
        state.counter = 0;
        state.result = "null";
    });
}
int set_host_call(void* raw, NorthStarScriptHostCall callback, void* user) {
    return guarded([&] {
        require(raw,"Missing script context");
        auto& state = *static_cast<MockContext*>(raw);
        state.host_call=callback;
        state.host_user=user;
    });
}
NorthStarScriptingApi const api{create,destroy,evaluate,reset,set_host_call};
NorthStarModule const module{sizeof(NorthStarModule),
    NORTHSTARCPP_MODULE_ABI, NORTHSTARCPP_CAPABILITY_SCRIPTING,
    "Mock Scripting", "2.0.0", NORTHSTARCPP_SCRIPTING_ABI,
    sizeof(api), &api, last_error};
}
extern "C" const NorthStarModule* northstar_module_query(
    std::uint32_t host, char const* capability, std::uint32_t version) {
    return match(module, host, capability, version);
}
