// NorthStar native Input Actions: context priority, remapping and UI capture.
// No dependency on Win32 messages, V8, renderer, or a particular input provider.
#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace northstarcpp::gameplay {
class InputActionMap final {
public:
    static constexpr std::size_t max_keys=512;
    struct Binding {
        std::string action;
        std::uint16_t key=0; // key codes from the active Input Provider
    };
    struct Context {
        std::string name;
        int priority=0;
        bool enabled=true;
        bool consumes_keys=true;
        std::vector<Binding> bindings;
    };
    explicit InputActionMap(bool use_defaults=true) {
        if(!use_defaults)return;
        add_context("Gameplay",10,true);
        for(auto const& key:std::array<std::pair<char const*,unsigned>,8>{{
            {"MoveForward",'W'},{"MoveBackward",'S'},{"MoveLeft",'A'},
            {"MoveRight",'D'},{"Jump",32u},{"Sprint",16u},
            {"Interact",'E'},{"Pause",27u}}})
            bind("Gameplay",key.first,key.second);
        add_context("Vehicle",20,false);
        for(auto const& key:std::array<std::pair<char const*,unsigned>,5>{{
            {"Accelerate",'W'},{"Brake",'S'},{"SteerLeft",'A'},
            {"SteerRight",'D'},{"ExitVehicle",'E'}}})
            bind("Vehicle",key.first,key.second);
        add_context("UI",100,false);
        bind("UI","Confirm",13u);
        bind("UI","Cancel",27u);
    }
    void add_context(std::string name,int priority,bool enabled=true,
                     bool consumes_keys=true) {
        if(!valid_name(name) || priority < -1000 || priority > 1000)
            throw std::invalid_argument("Invalid input context");
        if(find_context(name))throw std::invalid_argument("Duplicate input context");
        if(contexts_.size()>=16)throw std::length_error("Too many input contexts");
        contexts_.push_back({std::move(name),priority,enabled,consumes_keys,{}});
        std::stable_sort(contexts_.begin(),contexts_.end(),[](auto const& a,auto const& b){
            return a.priority>b.priority;
        });
    }
    void set_context_enabled(std::string_view name,bool enabled) {
        auto* context=find_context(name);
        if(!context)throw std::invalid_argument("Unknown input context");
        context->enabled=enabled;
    }
    bool context_enabled(std::string_view name) const {
        auto const* context=find_context(name);
        if(!context)throw std::invalid_argument("Unknown input context");
        return context->enabled;
    }
    void bind(std::string_view context_name,std::string action,unsigned key) {
        if(!valid_name(action) || key>=max_keys)
            throw std::invalid_argument("Invalid action name or key");
        auto* context=find_context(context_name);
        if(!context)throw std::invalid_argument("Unknown input context");
        for(auto const& existing:context->bindings)
            if(existing.key==key && existing.action!=action)
                throw std::invalid_argument("Key already bound in this input context");
        for(auto& existing:context->bindings)
            if(existing.action==action) {
                existing.key=static_cast<std::uint16_t>(key);
                return;
            }
        if(context->bindings.size()>=64)
            throw std::length_error("Too many bindings in input context");
        context->bindings.push_back({std::move(action),static_cast<std::uint16_t>(key)});
    }
    bool action_down(std::string_view action,std::uint8_t const* keys,
                     std::size_t key_count,bool ui_captures=false) const noexcept {
        if(!keys || !key_count)return false;
        std::array<bool,max_keys> consumed{};
        bool result=false;
        for(auto const& context:contexts_) {
            const bool ui=context.name=="UI";
            if(ui_captures && context.name!="UI")continue;
            if(!context.enabled && !(ui && ui_captures))continue;
            for(auto const& binding:context.bindings) {
                auto const index=std::size_t(binding.key);
                if(index>=key_count || !keys[index] || consumed[index])continue;
                if(binding.action==action)result=true;
                if(context.consumes_keys)consumed[index]=true;
            }
        }
        return result;
    }
    std::vector<Context> const& contexts() const noexcept {return contexts_;}
private:
    static bool valid_name(std::string_view name) {
        if(name.empty() || name.size()>64)return false;
        for(char c:name)if(!((c>='a' && c<='z')||(c>='A' && c<='Z')||
                             (c>='0' && c<='9')||c=='_'||c=='.'))
            return false;
        return true;
    }
    Context* find_context(std::string_view name) {
        for(auto& context:contexts_)if(context.name==name)return &context;
        return nullptr;
    }
    Context const* find_context(std::string_view name) const {
        for(auto const& context:contexts_)if(context.name==name)return &context;
        return nullptr;
    }
    std::vector<Context> contexts_;
};
} // namespace northstarcpp::gameplay
