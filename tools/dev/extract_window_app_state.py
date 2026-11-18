#!/usr/bin/env python3
"""One-shot, transaction-guarded extraction of App's runtime state.

No public ABI. Keep all field declaration order in the new private base,
and transfer only constructor initialization to the private base constructor.
"""
from pathlib import Path
import hashlib
repo=Path(__file__).resolve().parents[2]
folder=repo/"engine/northstarcpp/modules/editor"
head=folder/"window_app_internal.hpp"
bootstrap=folder/"window_runtime_bootstrap.inc"
out=folder/"window_app_state.hpp"
if out.exists():raise RuntimeError("window_app_state.hpp exists: abort")
original=head.read_bytes(); boot_original=bootstrap.read_bytes()
s=original.decode("utf-8").replace("\r\n","\n"); b=boot_original.decode("utf-8").replace("\r\n","\n")
start=s.index("    Options config;\n")
finish=s.index("\n};\n\n} // namespace window_app_internal",start)
fields=s[start:finish]
assert "runtime_services::RuntimeServices runtime_services_;" in fields
assert "timing::FrameClock frame_clock" in fields
assert "ScreenPoint camera_pointer" in fields
assert "AppRuntimeState" not in s
assert 'class App {' in s
assert b.count(": config(std::move(settings)), startup_report_(startup_report),")==1
new_state=(
    "#pragma once\n"
    "// App-only state: order of members is preserved to keep resource lifetime rules.\n"
    "// Included from window_app_internal.hpp inside northstarcpp::window_app_internal.\n"
    "struct AppRuntimeState {\n"
    "    using ScreenPoint = editor_tools::ScreenPoint;\n"
    "    explicit AppRuntimeState(Options settings, diagnostics::StartupReport* startup_report)\n"
    "        : config(std::move(settings)), startup_report_(startup_report),\n"
    "          runtime_services_(config,file_path) {}\n"
    +fields+"\n};\n")
s_new=s[:start]+s[finish:]
s_new=s_new.replace("class App {","// All mutable App members live in one private state base.\n"
    '#include "window_app_state.hpp"\nclass App : private AppRuntimeState {',1)
# Oops: the header is included within namespace and before class. The comment
# and preprocessor include preserve the single named App definition.
b_new=b.replace(": config(std::move(settings)), startup_report_(startup_report),\n"
                "          runtime_services_(config,file_path)",
                ": AppRuntimeState(std::move(settings),startup_report)",1)
assert b_new!=b and s_new!=s
for name,data in (("window_app_header_before_state",original),
                  ("window_bootstrap_before_state",boot_original)):
    saved=repo/"out/refactor_backups"/(name+"_"+hashlib.sha256(data).hexdigest()[:12]+".original")
    saved.parent.mkdir(parents=True,exist_ok=True)
    if not saved.exists():saved.write_bytes(data)
assert head.read_bytes()==original and bootstrap.read_bytes()==boot_original,"Concurrent edit"
out.write_text(new_state,encoding="utf-8")
head.write_bytes(s_new.replace("\n","\r\n").encode("utf-8") if b"\r\n" in original else s_new.encode("utf-8"))
bootstrap.write_bytes(b_new.replace("\n","\r\n").encode("utf-8") if b"\r\n" in boot_original else b_new.encode("utf-8"))
print("App header bytes",len(original),"->",head.stat().st_size,
      "state header",out.stat().st_size)
print("App state members retain original order, initialized in private base constructor")
