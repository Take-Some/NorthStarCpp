#!/usr/bin/env python3
"""One-shot migration: domain-scoped typed ScriptRuntime callback contracts.

The real registrar has no App header. The App-dependent host remains separate,
and only the Input callbacks move entirely to an App-independent native TU.
"""
from pathlib import Path
from collections import Counter
import hashlib
import re

repo=Path(__file__).resolve().parents[2]
folder=repo/"engine/northstarcpp/modules/editor"
old=folder/"window_script_bindings.cpp"
host=folder/"window_script_bindings_host.cpp"
contract=folder/"window_script_domain_contracts.hpp"
core=folder/"window_script_bindings.cpp"
cmake=folder.parent.parent/"CMakeLists.txt"
mods={k:folder/("window_script_"+k+"_bindings.cpp") for k in
      ("scene","camera","ecs","input","player","audio")}
for out in (host,contract,*mods.values()):
    if out.exists():raise RuntimeError(f"Already migrated: {out}")
before=old.read_bytes(); c_before=cmake.read_bytes()
normalize=lambda x: x.replace(b"\r\n",b"\n").decode("utf-8")
s=normalize(before)
c=normalize(c_before)
domains=("scene","camera","ecs","input","player","audio")
actual={d:re.findall(r'runtime\.bind\("engine\.'+d+r'","([^"]+)"',s) for d in domains}
expected={"scene":15,"camera":23,"ecs":8,"input":5,"player":12,"audio":6}
assert {d:len(x) for d,x in actual.items()}==expected
assert all(len(x)==len(set(x)) for x in actual.values())
assert s.count("void App::configure_script_bindings(ScriptRuntime& runtime)")==1
assert s.count("configure_vehicle_bindings(runtime);")==1
assert s.count("bind_project_save_api(runtime, ScriptSaveBindingsContext{")==1
assert '    }\n\n} // namespace northstarcpp::window_app_internal\n' in s

original_input_start='        runtime.bind("engine.input","isDown",'
original_input_end='        runtime.bind("engine.camera","available",'
i=s.index(original_input_start)
j=s.index(original_input_end,i)
input_impl=s[i:j]
assert len(re.findall(r'runtime\.bind\("engine\.input"',input_impl))==5

# Native app-independent input callback implementation, extracted byte-for-byte
# except replacing App member names with explicit typed InputStateView fields.
input_changes=[
    ('runtime.bind("engine.input","','bindings.add("'),
    ('[this]','[state]'),
    ('script_args(', 'runtime_script_contracts::script_args('),
    ('script_index(', 'runtime_script_contracts::script_index('),
    ('script_keys.size()', 'state.key_count'),
    ('script_keys[', 'state.keys['),
    ('script_mouse_buttons.size()', 'state.mouse_button_count'),
    ('script_mouse_buttons[','state.mouse_buttons['),
    ('script_mouse_dx','*state.pointer_dx'),
    ('script_mouse_dy','*state.pointer_dy'),
    ('script_wheel_vertical','*state.wheel_vertical'),
    ('script_gamepads.size()','state.gamepad_count'),
    ('script_gamepads[','state.gamepads['),
]
for before_text, after_text in input_changes:
    input_impl=input_impl.replace(before_text,after_text)
assert "runtime.bind" not in input_impl
assert "[this]" not in input_impl
assert "script_keys" not in input_impl

new_input_call='''        collect_input_script_bindings(domains.input, InputBindingStateView{
            script_keys.data(), script_keys.size(),
            script_mouse_buttons.data(), script_mouse_buttons.size(),
            &script_mouse_dx, &script_mouse_dy, &script_wheel_vertical,
            script_gamepads.data(), script_gamepads.size()
        });
'''
s=s[:i]+new_input_call+s[j:]
# Group only existing six-domain App callbacks. Save, vehicle, UI, physics,
# logger and others continue to bind in their original order, preserving their
# public runtime behavior.
for d in domains:
    s=s.replace(f'runtime.bind("engine.{d}","',
                f'domains.{d}.add("')
assert 'runtime.bind("engine.input"' not in s
assert "void App::configure_script_bindings(ScriptRuntime& runtime)" in s
s=s.replace(
  "void App::configure_script_bindings(ScriptRuntime& runtime) {\n",
  "void App::configure_script_bindings(ScriptRuntime& runtime) {\n    ScriptDomainBindings domains;\n",1)
closing='    }\n\n} // namespace northstarcpp::window_app_internal\n'
assert s.endswith(closing)
s=s[:-len(closing)]+(
  "        // Bind scoped capabilities after their App-owned callbacks exist.\n"
  "        // The registry and domain TUs do not include the App definition.\n"
  "        bind_script_domains(runtime, std::move(domains));\n"
  +closing)
s=s.replace('#include "window_script_bindings.hpp"',
            '#include "window_script_bindings.hpp"\n#include "window_script_domain_contracts.hpp"',1)
assert sum(s.count(f'domains.{d}.add(') for d in domains)==sum(expected.values())-expected["input"]
assert "collect_input_script_bindings(domains.input" in s

H='''#pragma once
// Internal, strongly typed domain callback tables. No App type is exposed.
// Compile-time method sets reject missing methods or accidental API drift.
#include <northstarcpp/scripting.hpp>
#include <northstarcpp/input.hpp>
#include <algorithm>
#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace northstarcpp::window_app_internal {

template<class Tag>
class TypedScriptCallbacks {
public:
    static constexpr std::size_t count = Tag::names.size();
    void add(std::string_view method, ScriptRuntime::ScriptHandler handler) {
        auto const it=std::find(Tag::names.begin(),Tag::names.end(),method);
        if(it==Tag::names.end())
            throw std::invalid_argument("Unknown scripted method in "+std::string(Tag::capability));
        auto const index=static_cast<std::size_t>(it-Tag::names.begin());
        if(callbacks_[index])
            throw std::invalid_argument("Duplicate scripted method in "+std::string(Tag::capability));
        if(!handler)throw std::invalid_argument("Empty scripted method handler");
        callbacks_[index]=std::move(handler);
    }
    void bind_all(ScriptRuntime& runtime) && {
        for(std::size_t i=0;i<callbacks_.size();++i) {
            if(!callbacks_[i])
                throw std::runtime_error(
                    "Missing "+std::string(Tag::capability)+"."+std::string(Tag::names[i])+" binding");
            runtime.bind(std::string(Tag::capability),std::string(Tag::names[i]),
                         std::move(callbacks_[i]));
        }
    }
private:
    std::array<ScriptRuntime::ScriptHandler,count> callbacks_{};
};

'''
for d in domains:
    struct=d.capitalize()+"ScriptTag"
    names=',\n        '.join('"'+x+'"' for x in actual[d])
    H+=f'''struct {struct} final {{
    static constexpr std::string_view capability="engine.{d}";
    inline static constexpr std::array<std::string_view,{len(actual[d])}> names{{{{
        {names}
    }}}};
}};
using {d.capitalize()}ScriptBindings=TypedScriptCallbacks<{struct}>;

'''
H+='''struct ScriptDomainBindings final {
'''
for d in domains:H+=f'    {d.capitalize()}ScriptBindings {d};\n'
H+='''};

'''
for d in domains:
    H+=f'void register_{d}_script_bindings(ScriptRuntime&, {d.capitalize()}ScriptBindings&&);\n'
H+='''
void bind_script_domains(ScriptRuntime&,ScriptDomainBindings&&);

// Read-only, host-owned input view. The ScriptRuntime and host callbacks never
// outlive the App session that owns these pointers. No pointer is exported to JS.
struct InputBindingStateView final {
    std::uint8_t const* keys=nullptr;
    std::size_t key_count=0;
    std::uint8_t const* mouse_buttons=nullptr;
    std::size_t mouse_button_count=0;
    float const* pointer_dx=nullptr;
    float const* pointer_dy=nullptr;
    std::int32_t const* wheel_vertical=nullptr;
    NorthStarInputGamepad const* gamepads=nullptr;
    std::size_t gamepad_count=0;
};
void collect_input_script_bindings(InputScriptBindings&,InputBindingStateView);

} // namespace northstarcpp::window_app_internal
'''
new_core='''#include "window_script_domain_contracts.hpp"
// App-agnostic registration coordinator. App state and Win32 types are not
// included, even indirectly, through window_app_internal.hpp.
namespace northstarcpp::window_app_internal {
void bind_script_domains(ScriptRuntime& runtime,ScriptDomainBindings&& domains) {
'''
for d in domains:new_core+=f'    register_{d}_script_bindings(runtime,std::move(domains.{d}));\n'
new_core+='}\n} // namespace northstarcpp::window_app_internal\n'
for d in domains:
    mod='''#include "window_script_domain_contracts.hpp"
namespace northstarcpp::window_app_internal {
'''
    if d=='input':
        mod='''#include "window_script_domain_contracts.hpp"
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
'''
        mod+=input_impl+'\n}\n'
    mod+=f'''void register_{d}_script_bindings(ScriptRuntime& runtime,
                                   {d.capitalize()}ScriptBindings&& handlers) {{
    std::move(handlers).bind_all(runtime);
}}
}} // namespace northstarcpp::window_app_internal
'''
    mods[d].write_text(mod,encoding="utf-8")
assert '    modules/editor/window_script_bindings.cpp\n' in c
insert='    modules/editor/window_script_bindings.cpp\n'
for d in domains:insert+=f'    modules/editor/window_script_{d}_bindings.cpp\n'
insert+='    modules/editor/window_script_bindings_host.cpp\n'
c=c.replace('    modules/editor/window_script_bindings.cpp\n',insert,1)

# Existing original body kept via host file; original name becomes App-free
# binder. All touched source files are backed up before replacement.
backup=repo/"out/refactor_backups"/("script_domain_bindings_before_"+hashlib.sha256(before).hexdigest()[:12]+".original")
backup.parent.mkdir(parents=True,exist_ok=True)
if not backup.exists():backup.write_bytes(before)
if old.read_bytes()!=before or cmake.read_bytes()!=c_before:
    raise RuntimeError("Concurrent edits to binding TU or CMake, abort")
host.write_text(s,encoding="utf-8")
contract.write_text(H,encoding="utf-8")
old.write_text(new_core,encoding="utf-8")
cmake.write_text(c,encoding="utf-8")
print("Domain split:", {d:len(actual[d]) for d in domains})
print("Host binder bytes",host.stat().st_size,"App-free core bytes",old.stat().st_size)
print("Input callbacks moved to App-free C++ file")
