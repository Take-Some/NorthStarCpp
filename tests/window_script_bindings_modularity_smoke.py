#!/usr/bin/env python3
"""Guard the App-free Scene, Camera, ECS, Input and scoped script binding boundary."""
from pathlib import Path
import re

repo=Path(__file__).resolve().parents[1]
editor=repo/"engine/northstarcpp/modules/editor"
load=lambda n:(editor/n).read_text("utf-8")
cmake=(repo/"engine/northstarcpp/CMakeLists.txt").read_text("utf-8")
main=load("window.cpp")
app=load("window_app_internal.hpp")
state=load("window_app_state.hpp")
host=load("window_script_bindings_host.cpp")
core=load("window_script_bindings.cpp")
contract=load("window_script_domain_contracts.hpp")
lifecycle=load("window_runtime_script_lifecycle.inc")
save=load("window_script_save_bindings.cpp")
domains={"scene":15,"camera":23,"ecs":8,"input":5,"player":12,"audio":6}

assert main.count("int WINAPI WinMain(")==1
assert "class App : private AppRuntimeState {" in app
assert '#include "window_app_state.hpp"' in app
assert "struct AppRuntimeState {" in state
assert "script_binding_leases" in state
assert "script_scene_generation" in state
assert "void App::configure_script_bindings(" in host
assert "void App::configure_script_bindings(" not in app+core
assert "bind_script_domains(runtime, std::move(domains));" in host
assert '#include "window_app_internal.hpp"' not in core
assert "class App" not in core
assert "void bind_script_domains(" in core
assert "bind_project_save_api(" in save
assert "window_app_internal.hpp" not in save
assert save.count('runtime.bind("engine.save"')==4
assert '#include "window_runtime_script_bindings.inc"' not in host+app+main

# Full domain lifetime: old context stays valid through stop event, candidate
# lease is only revoked on failed reload, old lease on successful swap.
assert "std::make_shared<ScriptBindingLease>()" in host
assert "domains.set_lifetime(lease)" in host
assert "lease->revoke()" in lifecycle
assert "script_binding_leases.clear();" in lifecycle
assert "++script_scene_generation;" in lifecycle
assert "Stale ScriptRuntime binding context" in contract
assert "std::weak_ptr<ScriptBindingLease>" in contract

for name,n in domains.items():
    module=load(f"window_script_{name}_bindings.cpp")
    assert 'window_app_internal.hpp' not in module
    assert "[this]" not in module
    assert f"void register_{name}_script_bindings(" in module
    assert f"register_{name}_script_bindings(runtime,std::move(domains.{name}));" in core
    assert re.search(rf"^\s*modules/editor/window_script_{name}_bindings\.cpp\s*$",cmake,re.M)
    assert re.search(rf"struct {name.capitalize()}ScriptTag final",contract)
    assert re.search(rf"std::array<std::string_view,{n}> names",contract)
    expected_native=n
    assert module.count('bindings.add("')==expected_native,(name,expected_native,module.count('bindings.add("'))
    assert f'domains.{name}.add("' not in host
    if name in {"scene","camera","ecs","player","audio"}:
        assert f'domains.{name}.add("' not in host
        assert f"collect_{name}_runtime_script_bindings(domains.{name}," in host
        assert f"collect_{name}_runtime_script_bindings(" in module
    elif name=="input":
        assert "collect_input_script_bindings(domains.input," in host
        assert "runtime_script_contracts::script_index" in module
    assert 'window_app_internal.hpp' not in module
    if name=="ecs":
        assert "require_live_index" in module
        assert "service.live_handle(handle,it->second)" in module
        assert "script_scene_generation" in host
    if name=="scene":
        assert "service.live_index(parent)" in module
        assert "service.destroy_entity" in module

assert "Duplicate scripted method" in contract
assert "Missing " in contract
assert "Unknown scripted method" in contract
assert "ScriptBindingLease" in contract
assert re.search(r"^\s*modules/editor/window_script_bindings_host\.cpp\s*$",cmake,re.M)
assert host.count('runtime.bind("engine.')+save.count('runtime.bind("engine.save"')+sum(domains.values())==108
print("PASS: All six domains App-free; 69 typed callbacks, 108 registrations; invalidation wired")
