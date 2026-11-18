#!/usr/bin/env python3
"""One-shot extraction of App::configure_script_bindings as a native TU.

The existing App's ABI/private methods remain unchanged.  A private header
defines the same App type to both the WinMain and the binding TU, while a
separate script-binding boundary exposes only its bind entry point.
This migration is deliberately guarded and backs up both original inputs.
"""
from pathlib import Path
import hashlib

root=Path(__file__).resolve().parents[2]
src=root/"engine/northstarcpp/modules/editor"
app=src/"window.cpp"
inc=src/"window_runtime_script_bindings.inc"
internal=src/"window_app_internal.hpp"
new_cpp=src/"window_script_bindings.cpp"
contract=src/"window_script_bindings.hpp"
for out in (internal,new_cpp,contract):
    if out.exists(): raise RuntimeError(f"Refusing to overwrite existing output: {out}")
app_before=app.read_bytes()
inc_before=inc.read_bytes()
normalize=lambda data: data.replace(b"\r\n",b"\n")
code=normalize(app_before)
bindings=normalize(inc_before)
marker=b'namespace northstarcpp {\nnamespace {\n'
assert code.count(marker)==1,"App namespace boundary changed"
class_start=code.index(b'class App {')
assert code.count(b'#include "window_runtime_script_bindings.inc"')==1
winmain=code.index(b'int WINAPI WinMain(')
app_end=code.rindex(b'} // namespace northstarcpp',0,winmain)
assert app_end>class_start
app_definition=code[:app_end]
app_definition=app_definition.replace(marker,
    b'namespace northstarcpp {\nnamespace window_app_internal {\n',1)
app_definition=app_definition.replace(
    b'#include "window_runtime_script_bindings.inc"',
    b'    // Defined out of line in window_script_bindings.cpp.\n'
    b'    void configure_script_bindings(ScriptRuntime& runtime);',1)
# Function definitions now live in a shared internal header and need proper
# ODR semantics across WinMain and the independent bindings translation unit.
app_definition=app_definition.replace(
    b'Options options() {',
    b'inline Options options() {',1)
app_definition=app_definition.replace(
    b'Scene read_document(fs::path const& path)',
    b'inline Scene read_document(fs::path const& path)',1)
for declaration in (b'constexpr bool editor',b'constexpr wchar_t window_class',
                    b'constexpr wchar_t viewport_class',b'constexpr UINT frame_timer',
                    b'constexpr UINT smoke_message'):
    assert declaration in app_definition
    app_definition=app_definition.replace(declaration,b'inline '+declaration,1)
end_ns=b'} // namespace\n'
assert app_definition.endswith(end_ns), repr(app_definition[-80:])
app_definition=app_definition[:-len(end_ns)]+b'} // namespace window_app_internal\n'
app_definition+=(
    b'using window_app_internal::App;\n'
    b'using window_app_internal::options;\n'
    b'} // namespace northstarcpp\n')
app_definition=b'#pragma once\n'+app_definition
# One narrow typed contract for public-to-private binding entry, with no
# duplicated App fields or exposed ScriptRuntime provider ABI.
binding_contract=b'''#pragma once
namespace northstarcpp {
class ScriptRuntime;
namespace window_app_internal {
class App;
}
}
'''
body_start=bindings.index(b'    void configure_script_bindings(ScriptRuntime& runtime)')
body_finish=bindings.index(b'    // Load project-relative JavaScript',body_start)
function=bindings[body_start:body_finish].strip()
assert function.startswith(b'void configure_script_bindings(')
assert function.endswith(b'}')
function=function.replace(b'void configure_script_bindings(',
    b'void App::configure_script_bindings(',1)
compiled=(b'#include "window_app_internal.hpp"\n'
          b'#include "window_script_bindings.hpp"\n\n'
          b'namespace northstarcpp::window_app_internal {\n\n'
          +function+b'\n\n'
          b'} // namespace northstarcpp::window_app_internal\n')
# The WinMain entry point now consumes one common internal App class.
main=b'#include "window_app_internal.hpp"\n\n'+code[winmain:]
assert b'App::configure_script_bindings(' in compiled
assert b'int WINAPI WinMain(' in main and main.count(b'int WINAPI WinMain(')==1
assert b'configure_script_bindings(ScriptRuntime& runtime);' in app_definition
assert b'#include "window_runtime_script_bindings.inc"' not in app_definition
backup=root/"out/refactor_backups"
backup.mkdir(parents=True,exist_ok=True)
for name,data in (("window_before_binding_tu",app_before),
                  ("window_script_bindings_before_tu",inc_before)):
    path=backup/(name+"_"+hashlib.sha256(data).hexdigest()[:12]+".original")
    if not path.exists(): path.write_bytes(data)
if app.read_bytes()!=app_before or inc.read_bytes()!=inc_before:
    raise RuntimeError("Concurrent modifications detected; refusing migration")
for path,data in ((internal,app_definition),(new_cpp,compiled),
                  (contract,binding_contract)):
    path.write_bytes(data)
app.write_bytes(main)
print("App/WinMain split: main=",len(main),
      " internal=",len(app_definition)," binding TU=",len(compiled))
print("Script API members unchanged; raw input backups recorded.")
