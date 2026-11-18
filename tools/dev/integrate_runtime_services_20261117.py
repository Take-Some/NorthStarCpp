from pathlib import Path
editor=Path("engine/northstarcpp/modules/editor")
boot=editor/"window_runtime_bootstrap.inc"
win=editor/"window.cpp"
cm=Path("engine/northstarcpp/CMakeLists.txt")
s=boot.read_text(encoding="utf8")
w=win.read_text(encoding="utf8")
c=cm.read_text(encoding="utf8")
assert "runtime_services_.resolve_visual_prefabs" not in s
assert "window_runtime_services.cpp" not in c
a=s.index('        if(!editor && !config.new_document && !file_path.empty())')
z=s.index('        dirty = config.new_document;',a)
s=s[:a]+'''        if(!editor && !config.new_document && !file_path.empty())
            bootstrap_stage("scene.visual_prefabs",[&] {
                auto resolved=runtime_services_.resolve_visual_prefabs(
                    document.snapshot(),[this](std::string const& error) {
                        bootstrap_warning("scene.visual_prefabs",error);
                    });
                if(resolved)document.load(std::move(*resolved));
            });
'''+s[z:]
a=s.index('        bootstrap_stage("script.discovery", [&] {')
z=s.index('        if(startup_report_)\n            startup_report_->info("script.mode",',a)
s=s[:a]+'''        bootstrap_stage("script.discovery", [&] {
            auto const mode=runtime_services_.discover_script_mode(document.snapshot());
            scripted_game=mode.scripted_game;
            scripted_first_person=mode.first_person;
        });
'''+s[z:]
s=s.replace(': config(std::move(settings)), startup_report_(startup_report)',
            ': config(std::move(settings)), startup_report_(startup_report),\n          runtime_services_(config,file_path)',1)
assert w.count('    fs::path file_path;')==1
w=w.replace('    fs::path file_path;',
            '    fs::path file_path;\n    // Constructed after the two referenced App members; destroyed first.\n    runtime_services::RuntimeServices runtime_services_;',1)
assert w.count('#include "window_runtime_options.hpp"')==1
w=w.replace('#include "window_runtime_options.hpp"',
            '#include "window_runtime_options.hpp"\n#include "window_runtime_services.hpp"',1)
assert c.count('\n    modules/editor/window_runtime_frame_scheduler.cpp\n')==1
c=c.replace('\n    modules/editor/window_runtime_frame_scheduler.cpp\n',
            '\n    modules/editor/window_runtime_frame_scheduler.cpp\n    modules/editor/window_runtime_services.cpp\n',1)
boot.write_text(s,encoding="utf8")
win.write_text(w,encoding="utf8")
cm.write_text(c,encoding="utf8")
print('RuntimeServices integrated; script discovery and prefab initialization moved')
