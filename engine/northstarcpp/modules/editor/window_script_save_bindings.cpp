#include "window_script_bindings.hpp"
#include "window_runtime_script_contracts.hpp"
#include "window_runtime_project_files.hpp"
#include <northstarcpp/scripting.hpp>
#include <stdexcept>
#include <string>
#include <utility>

namespace northstarcpp::window_app_internal {

void bind_project_save_api(ScriptRuntime& runtime, ScriptSaveBindingsContext context)
{
    if (!context.open_current_project_store)
        throw std::invalid_argument("Missing project-scoped save capability");
    // All 4 callbacks retain the same lifetime as ScriptRuntime. This file
    // does not include the App type or access the App's private fields.
    auto store=std::move(context.open_current_project_store);
    runtime.bind("engine.storage.saves","write",[store](std::string_view json) {
        auto args=runtime_script_contracts::script_args(json);
        if (args.size()!=2) throw std::invalid_argument(
            "engine.save.write(slot,JSON.stringify(state))");
        store().write(args[0],args[1]);
        return std::string("true");
    });
    runtime.bind("engine.storage.saves","read",[store](std::string_view json) {
        auto args=runtime_script_contracts::script_args(json);
        if (args.size()!=1) throw std::invalid_argument("engine.save.read(slot)");
        auto value=store().read(args[0]);
        return value ? runtime_project_files::script_json_quote(*value) : std::string("null");
    });
    runtime.bind("engine.storage.saves","exists",[store](std::string_view json) {
        auto args=runtime_script_contracts::script_args(json);
        if (args.size()!=1) throw std::invalid_argument("engine.save.exists(slot)");
        return store().exists(args[0])
            ? std::string("true") : std::string("false");
    });
    runtime.bind("engine.storage.saves","remove",[store](std::string_view json) {
        auto args=runtime_script_contracts::script_args(json);
        if (args.size()!=1) throw std::invalid_argument("engine.save.remove(slot)");
        return store().erase(args[0])
            ? std::string("true") : std::string("false");
    });
}
} // namespace northstarcpp::window_app_internal
