# NorthStarCpp Editor — P0 Mixed Details, Vulkan/D3D12 hardware QA, PIE Step

Date: 2026-11-15
Root: `C:\Users\Kayla\Documents\NorthStar\NorthStarCpp`
Isolated Release QA build: `_qa_editor_mixed`

## Changes: P0 Details / mixed-value multi-selection

Qt Editor's World Outliner multi-selection and atomic Undo already existed, along with external DLL C ABI reflected scalar metadata. This iteration fixes a concrete omission: using only the selected primary actor's property value to infer whether batch editing occurred. If the primary already held the requested value but the other selected actors did not, the change was lost.

**New behavior**
- Each of 11 numeric actor fields (position XYZ, rotation XYZ, scale XYZ, size, mass) detects heterogeneous values across selected entity IDs. `mixedValues=true` is exposed as a widget property and a `*` suffix marks the mixed UI state; numeric content remains the primary actor's value.
- Inspector tracks explicitly user-edited scalar controls. A batch commit changes **only those controls**, including when a committed value equals the primary actor's existing value.
- Visibility and collision booleans use a native Qt tristate `PartiallyChecked` for heterogeneous selection. Only a user click converts that state into an actual batch write. No inadvertent uncheck on Apply.
- Aggregated collision shape/split/offset authoring remains single-object-only when multiple actors are selected: blindly copying an entire collider record would overwrite unrelated collider choices. A later schema-aware per-field batch editor should replace this safeguard.
- Every batch change remains a single validated `editScene` transaction and one Undo record; unrelated fields retain their original values.

**Files**
- `modules/editor/qt_editor_window.hpp`
- `modules/editor/qt_editor_window.cpp`
- `modules/editor/qt_editor_window_setup.cpp`
- `modules/editor/qt_editor_window_smoke.cpp`

**Regression evidence**: `northstarcpp_qt_editor_authoring` passed. The smoke now tests reapplying a primary actor's unchanged mixed X-value to other selections, atomic Undo and preservation of Y, plus tri-state visibility click, atomic Undo and original-state restoration.

## Changes: P1 Play-in-Editor — one-frame Step

Pause/Resume/Eject/Possess and Escape restoration were already implemented with pointer-free Win32 IPC and Player process isolation. Added Step without suspending the process:

- New `NorthStarCpp.PlayInEditor.Step.v1` and `StepRequest.v1` registered messages, without changing existing message names.
- Player-side `PieFrameStepper` queues at most one request while PIE is paused. The simulation advances **exactly 1/60 second** on the next Player frame and immediately returns to paused state. Raw frame wall time is never integrated after a pause or on a step.
- Script-originated pause prevents a manual step from bypassing its state. Pause/Resume reset the one-frame queue.
- Qt Editor adds `Step One Frame` menu action, **F9** shortcut and a sixth toolbar button with a native-drawn icon. Player F9 is forwarded to Qt through a versioned request; the Editor calls `sendPieCommand` only when the correct embedded Player HWND/PID is attached.
- `PieCommandAvailability.step` gates the action to active attached paused sessions.
- The Player window continues message pumping, drawing, and asset streaming while simulation is paused. Authoring state and the running Player scene remain in separate processes.

**Files**
- `modules/editor/qt_pie_protocol.hpp`
- `modules/editor/qt_pie_command_state.hpp`
- `modules/editor/window.cpp`
- `modules/editor/qt_editor_window.hpp`
- `modules/editor/qt_editor_window_play.cpp`
- `modules/editor/qt_editor_window_setup.cpp`
- `modules/editor/qt_play_surface.hpp`
- `modules/editor/qt_play_surface.cpp`
- `modules/editor/qt_editor_window_smoke.cpp`
- `tests/qt_pie_command_state_smoke.cpp`

**Evidence**:
- The pure PIE state test covers all 32 possible control states and `PieFrameStepper` queue/reset/fixed-delta behavior: PASS.
- The WARP native-render embedded PIE integration test sends Pause -> Step -> Resume -> Eject -> Possess -> Escape and verifies the Player acknowledged Step while paused; verbose output: `PIE controls: pause=1 step=1 resume=1 eject=1 possess=1`, PASS.
- The Qt authoring smoke confirms six PIE toolbar controls and disabled Step outside a running session.
- Actual frame-count instrumentation on a real Night City scene is **not yet implemented**. The fixed-step state model and IPC ACK are verified, but this smoke does not directly inspect physics positions after the step.

## P0/P2 hardware verification — Vulkan and D3D12

Both providers were already available and hardware-driven. **No new Vulkan/D3D12 GPU ID buffer is claimed here.** Their optional render-pick extension still reports `gpu_pick_supported=false`; current Qt viewport uses geometry picking fallback where necessary. The earlier D3D11 GPU-ID implementation was limited to primitive cubes.

Fresh, verified test hardware: NVIDIA Quadro RTX 4000, non-software device. Both 1000-frame full provider probes completed with 1000 valid timestamp samples and no frame failures.

| Metric (ms) | Vulkan 0.1.0 | D3D12 1.6.0 |
|---|---:|---:|
| GPU p50 | 0.034272 | 1.73077 |
| GPU p95 | 0.043008 | 3.48032 |
| GPU p99 | 0.0459853 | 4.43792 |
| CPU draw p50 | 16.6501 | 0.524 |
| CPU draw p95 | 17.7475 | 1.97536 |
| CPU draw p99 | 21.0488 | 3.07647 |
| Valid GPU samples | 1000 | 1000 |
| GPU pick supported | no | no |

These timings come from **lightweight provider probes**, not comparable whole-frame Night City benchmarks. Vulkan CPU draw p50 near 16.65 ms may include frame pacing/presentation; its GPU timestamp measures a smaller slice. Do not infer final backend superiority from this table.

CTest:
- `northstarcpp_device_probe_d3d12_stress`: PASS
- `northstarcpp_device_probe_d3d12_hardware`: PASS
- `northstarcpp_device_probe_vulkan_stress`: PASS
- `northstarcpp_device_probe_vulkan_hardware`: PASS

## Build / integration

- CMake+Ninja Release (MinGW GCC 16.2) configured under `_qa_editor_mixed`.
- Official V8 15.8.62 runtime fetched using verified TLS, SHA256 pinned to `9924fe95ae8a05b35f728da8258bd1d72a8810edc0179d65fd3018c7845489e3`. Runtime reported the exact expected version; no TLS bypass.
- Initial clean Editor build: **564 / 564 steps, successfully linked `northstarcpp_editor_qt.exe` and `northstarcpp_player.exe`**.
- Later incremental build after adding Step: **PASS**, Qt Editor and Player both linked.
- PIE command state, Qt authoring, embedded WARP render/IPC, embedded Escape workspace restoration: **4/4 PASS**.
- DLL ECS lifecycle test: PASS after building its stand-alone fixture in this QA tree.
- `git diff --check`: exit code 0 (workspace has existing LF->CRLF conversion warnings).
- One unrelated concurrent Qt prefab collision editor compile error for incomplete `QLabel` was fixed by explicitly including `<QLabel>` in `qt_prefab_collision_editor.hpp`.

## Remaining technical work, prioritized

1. **P0 GPU** — dedicated per-entity integer-ID render attachment/readback for Vulkan and D3D12, plus native YDD/YDR mesh IDs and skinning, and async readback with high-DPI coordinate correction. Long-run resize, device-lost and `0xC0000005` stress matrix.
2. **P0 Details** — batch ECS component editing with per-field dirty tracking across heterogeneous payloads; mixed-value formatting for nested reflected fields; external DLL schema versions/enum/string/array metadata. Collision compound per-field batch edit.
3. **P1 World Editor** — World Partition cell authoring, HLOD, Data Layers and NavMesh visualization/editing.
4. **P1 Content Browser** — bounded async NEF8 library indexing with dependency graph and large-library latency/memory measurements.
5. **P1 PIE** — direct physics-position frame-step validation and richer runtime state snapshot/restore tests; current Pause/Step/Resume/Eject/Possess/Escape controls are connected and smoke-tested.
6. **P2 Profiling** — cross-thread aligned CPU/GPU traces, driver timestamps and p50/p95/p99 on real 10,000+ actor scenes.

No production executable replacement, Git commit or push. User's pre-existing working tree edits are preserved; the separate QA build directory is not intended for source control.
