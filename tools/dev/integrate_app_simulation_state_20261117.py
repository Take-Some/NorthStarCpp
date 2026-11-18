from pathlib import Path
import re
folder=Path("engine/northstarcpp/modules/editor")
source=folder/"window.cpp"
x=source.read_text(encoding="utf8")
old='''    bool pie_paused=false;
    bool pie_ejected=false;
    northstarcpp::editor::PieFrameStepper pie_stepper;'''
assert x.count(old)==1
x=x.replace(old,'    runtime_services::AppSimulationState runtime_state_;',1)
assert x.count('#include "window_runtime_services.hpp"')==1
x=x.replace('#include "window_runtime_services.hpp"',
            '#include "window_runtime_services.hpp"\n#include "window_runtime_app_state.hpp"',1)
files={source:x}
for inc in ['window_runtime_frame_loop.inc','window_runtime_win32_messages.inc']:
 p=folder/inc
 files[p]=p.read_text(encoding="utf8")
for p,content in files.items():
 for name in ['pie_paused','pie_ejected','pie_stepper']:
  content=re.sub(r'\b'+name+r'\b','runtime_state_.'+name,content)
 assert 'runtime_state_.runtime_state_' not in content
 p.write_text(content,encoding="utf8")
print("Moved PIE pause/eject/stepper into AppSimulationState with no extra allocations")
