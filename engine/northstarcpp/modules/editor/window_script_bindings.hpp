#pragma once
#include <functional>
#include <northstarcpp/save_game.hpp>
namespace northstarcpp {
class ScriptRuntime;
namespace window_app_internal {
class App;

// Narrow capability, not a reference to the whole App. The callback resolves
// the current project/scene path on each call (save-as remains supported).
struct ScriptSaveBindingsContext {
    std::function<SaveGameStore()> open_current_project_store;
};

void bind_project_save_api(ScriptRuntime& runtime,ScriptSaveBindingsContext context);
} // namespace window_app_internal
} // namespace northstarcpp
