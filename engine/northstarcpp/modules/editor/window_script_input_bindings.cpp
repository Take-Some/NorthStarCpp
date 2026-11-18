#include "window_script_domain_contracts.hpp"
#include "window_runtime_script_contracts.hpp"
#include <stdexcept>
#include <string>

namespace northstarcpp::window_app_internal {

void collect_input_script_bindings(InputScriptBindings& bindings,
                                   InputBindingStateView state) {
    if(!state.keys || !state.mouse_buttons ||
       !state.pointer_dx || !state.pointer_dy || !state.wheel_vertical ||
       !state.gamepads || !state.key_count || !state.mouse_button_count ||
       !state.gamepad_count)
        throw std::invalid_argument("Invalid App input binding state view");
        bindings.add("isDown",
            [state](std::string_view json) {
                auto args=runtime_script_contracts::script_args(json);
                if (args.size()!=1) throw std::invalid_argument("isDown(keyCode)");
                auto const index=runtime_script_contracts::script_index(args[0]);
                if (index>=state.key_count) throw std::out_of_range("Key code out of range");
                return state.keys[index] ? std::string("true") : std::string("false");
            });


        bindings.add("actionDown",[state](std::string_view json) {
            auto const args=runtime_script_contracts::script_args(json);
            if(args.size()!=1 || !state.actions)
                throw std::invalid_argument("input.actionDown(actionName)");
            const bool uiCaptured=(state.script_paused && *state.script_paused) ||
                                  (state.menu_visible && *state.menu_visible);
            return state.actions->action_down(args[0],state.keys,state.key_count,uiCaptured)
                ?std::string("true"):std::string("false");
        });
        bindings.add("rebindAction",[state](std::string_view json) {
            auto const args=runtime_script_contracts::script_args(json);
            if(args.size()!=3 || !state.actions)
                throw std::invalid_argument("input.rebindAction(context,action,keyCode)");
            auto const key=runtime_script_contracts::script_index(args[2]);
            auto previous=*state.actions;
            state.actions->bind(args[0],args[1],static_cast<unsigned>(key));
            if(state.save_actions) {
                try {state.save_actions();}
                catch(...) {*state.actions=std::move(previous);throw;}
            }
            return std::string("true");
        });
        bindings.add("setContextEnabled",[state](std::string_view json) {
            auto const args=runtime_script_contracts::script_args(json);
            if(args.size()!=2 || !state.actions ||
               (args[1]!="true" && args[1]!="false"))
                throw std::invalid_argument("input.setContextEnabled(context,boolean)");
            state.actions->set_context_enabled(args[0],args[1]=="true");
            return std::string("null");
        });
        bindings.add("mouseDown",[state](std::string_view json) {
            auto args=runtime_script_contracts::script_args(json);
            if (args.size()!=1) throw std::invalid_argument("input.mouseDown(index)");
            auto idx=runtime_script_contracts::script_index(args[0]);
            if (idx>=state.mouse_button_count) throw std::out_of_range("Mouse button index");
            return state.mouse_buttons[idx] ? std::string("true") : std::string("false");
        });
        bindings.add("pointerDelta",[state](std::string_view json) {
            if (!runtime_script_contracts::script_args(json).empty()) throw std::invalid_argument("input.pointerDelta()");
            return std::string("{\"x\":")+std::to_string(*state.pointer_dx)+
                ",\"y\":"+std::to_string(*state.pointer_dy)+"}";
        });
        bindings.add("wheelDelta",[state](std::string_view json) {
            if (!runtime_script_contracts::script_args(json).empty()) throw std::invalid_argument("input.wheelDelta()");
            // One wheel notch is 120 input-provider units; keep precision for trackpads.
            return std::to_string(static_cast<double>(*state.wheel_vertical)/120.0);
        });
        bindings.add("gamepad",[state](std::string_view json) {
            auto args=runtime_script_contracts::script_args(json);
            if(args.size()!=1) throw std::invalid_argument("input.gamepad(index)");
            auto index=runtime_script_contracts::script_index(args[0]);
            if(index>=state.gamepad_count) throw std::out_of_range("Gamepad index");
            auto const& p=state.gamepads[index];
            if(!p.connected) return std::string("null");
            return std::string("{\"buttons\":")+std::to_string(p.buttons)+
                ",\"leftX\":"+std::to_string(p.left_x)+
                ",\"leftY\":"+std::to_string(p.left_y)+
                ",\"rightX\":"+std::to_string(p.right_x)+
                ",\"rightY\":"+std::to_string(p.right_y)+
                ",\"leftTrigger\":"+std::to_string(p.left_trigger)+
                ",\"rightTrigger\":"+std::to_string(p.right_trigger)+"}";
        });

}
void register_input_script_bindings(ScriptRuntime& runtime,
                                   InputScriptBindings&& handlers) {
    std::move(handlers).bind_all(runtime);
}
} // namespace northstarcpp::window_app_internal
