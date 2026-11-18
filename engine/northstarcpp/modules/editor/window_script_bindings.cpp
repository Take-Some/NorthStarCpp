#include "window_script_domain_contracts.hpp"
// App-agnostic registration coordinator. App state and Win32 types are not
// included, even indirectly, through window_app_internal.hpp.
namespace northstarcpp::window_app_internal {
void bind_script_domains(ScriptRuntime& runtime,ScriptDomainBindings&& domains) {
    register_scene_script_bindings(runtime,std::move(domains.scene));
    register_camera_script_bindings(runtime,std::move(domains.camera));
    register_ecs_script_bindings(runtime,std::move(domains.ecs));
    register_input_script_bindings(runtime,std::move(domains.input));
    register_player_script_bindings(runtime,std::move(domains.player));
    register_audio_script_bindings(runtime,std::move(domains.audio));
}
} // namespace northstarcpp::window_app_internal
