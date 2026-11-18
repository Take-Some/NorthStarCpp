#include "../engine/northstarcpp/modules/editor/window_script_domain_contracts.hpp"
#include <stdexcept>
#include <string>
#include <string_view>
#include <cstdio>
using namespace northstarcpp::window_app_internal;

void require(bool value,char const* what) {
    if(!value)throw std::runtime_error(what);
}
template<class Bindings,class Tag>
void check_domain() {
    Bindings callbacks;
    require(!callbacks.complete(),"New domain must not be complete");
    auto fn=[](std::string_view) {return std::string("null");};
    bool unknown=false;
    try { callbacks.add("__not_a_real_method__",fn); }
    catch(std::invalid_argument const&) {unknown=true;}
    require(unknown,"Unknown ScriptRuntime method not rejected");
    for(auto const name:Tag::names)callbacks.add(name,fn);
    require(callbacks.complete(),"All supported handlers should complete this domain");
    bool duplicate=false;
    try {callbacks.add(Tag::names.front(),fn);}
    catch(std::invalid_argument const&) {duplicate=true;}
    require(duplicate,"Duplicate ScriptRuntime method not rejected");
    bool empty=false;
    Bindings empty_callbacks;
    try{ empty_callbacks.add(Tag::names.front(),{});}
    catch(std::invalid_argument const&){empty=true;}
    require(empty,"Empty handler accepted");
}
int main(){
    try {
        check_domain<SceneScriptBindings,SceneScriptTag>();
        check_domain<CameraScriptBindings,CameraScriptTag>();
        check_domain<EcsScriptBindings,EcsScriptTag>();
        check_domain<InputScriptBindings,InputScriptTag>();
        check_domain<PlayerScriptBindings,PlayerScriptTag>();
        check_domain<AudioScriptBindings,AudioScriptTag>();
        // Failed hot reload must invalidate only the candidate lease.
        auto active=std::make_shared<ScriptBindingLease>();
        auto candidate=std::make_shared<ScriptBindingLease>();
        require(active->valid() && candidate->valid(),"New session token is invalid");
        candidate->revoke();
        require(!candidate->valid() && active->valid(),
                "Failed candidate invalidated the old live session");
        std::weak_ptr<ScriptBindingLease> weak=active;
        active->revoke();
        require(!active->valid(),"Script stop did not revoke lease");
        active.reset();
        require(weak.expired(),"Dead session lease is retained");
        std::puts("Script domain contracts: PASS (6 domains, handlers and context leases)");
        return 0;
    }catch(std::exception const& e) {
        std::fprintf(stderr,"Script domain contracts: FAIL %s\n",e.what());
        return 1;
    }
}
