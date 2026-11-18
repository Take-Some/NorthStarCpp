# NorthStarCpp — Animation Graph v1 (2026-11-17)

## Scope and status

Implemented a new **XML-authored, provider-neutral, opt-in** animation evaluation path. The existing `engine.model.animation` YCD reference is retained for compatibility. The graph never converts the YMT/YCD assets to JSON, modifies imported skeleton bind data, or bypasses the existing row-vector palette and GPU/CPU skinning pipeline.

Public API: `engine/northstarcpp/modules/api/include/northstarcpp/animation_graph.hpp`.

Runtime sources:

- `modules/animation/animation_graph_xml.cpp` — XML reader, bounded schema validation, path and reference validation
- `modules/animation/animation_graph_runtime.cpp` — Blend1D, transitions, clip synchronization, masked additive/override layers, bounded notifies
- `modules/animation/animation_graph_async.cpp` — asynchronous VFS requests and bounded CPU background decoding
- `modules/animation/skeleton.cpp` — consume fully blended local transforms using the existing YMT-derived inverse bind and secondary/helper constraints
- `modules/viewport/viewport.cpp` — per-entity opt-in animation graphs and reuse of the existing two-hand IK/skin uploads

## XML authoring example

```xml
<?xml version="1.0" encoding="utf-8"?>
<AnimationGraph schema="1" initial="locomotion">
  <Parameters>
    <Float name="speed" default="0"/>
    <Float name="attack" default="0"/>
    <Float name="aim" default="0"/>
  </Parameters>
  <States>
    <State name="locomotion" sync="gait">
      <Blend1D parameter="speed">
        <Point at="0" ref="characters/abby/animations/mm-explore.ycd@abby-mm-explore-idle"/>
        <Point at="1" ref="characters/abby/animations/mm-explore.ycd@abby-mm-explore-walk-loop-fw"/>
      </Blend1D>
    </State>
    <State name="attack"><Clip ref="animations/combat.ycd@attack"/></State>
  </States>
  <Transitions>
    <Transition from="locomotion" to="attack" parameter="attack"
                op="ge" value="0.5" fade="0.15"/>
    <Transition from="attack" to="locomotion" parameter="attack"
                op="le" value="0.1" fade="0.15"/>
  </Transitions>
  <Layers>
    <Layer name="upper_body" ref="animations/combat.ycd@aim"
           mode="additive" roots="spineb,l_shoulder,r_shoulder"
           weight_parameter="aim" weight="1"/>
  </Layers>
  <Events>
    <Notify ref="animations/combat.ycd@attack" name="attack_impact" at="0.4"/>
  </Events>
</AnimationGraph>
```

The combat reference above illustrates syntax; supply actual YCD files/clips before use.
A **real, loadable** example for bundled Abby assets is at:

`engine/northstarcpp/resources/sharedAssets/animations/abby_locomotion.nagraph.xml`

## Viewport integration

Add the following v1 components to an entity already equipped with a native YDD/YMT rig:

- `engine.model.animation_graph` payload: `animations/abby_locomotion.nagraph.xml`
- `engine.model.animation_parameter` payload: `speed 0.5`

Multiple parameter components may be authored per entity (up to 32). Values are read from the immutable scene components on each frame. The new graph has precedence if both it and the legacy `engine.model.animation` are present.

Animation playback is entity-owned. Clips are decoded once and cached per native rig; the `AnimationGraphInstance` owns only parameters, state, transition clocks, masks, event records and reusable pose scratch. The final pose is fed through `SkeletonPose::set_local_transforms`; attachments and first-person pistol grip use the same imported skeleton and the existing IK pipeline.

## Asset Browser and XML editing

Graph files named `*.nagraph.xml` are classified as Animation in Asset Browser and shown with an animation icon. They open in the text editor as XML. A visual node/port editor is not yet implemented.

## Blending and synchronization

- `Blend1D` points are sorted and validated at graph compile time.
- Blend samples use quaternion slerp for joint rotations, linear translation and scale interpolation.
- If a state declares `sync=`, active Blend1D sources are sampled at the same normalized cycle phase; the dominant blend source supplies the cycle period.
- Transitions support `ge` and `le` parameter comparisons and bounded crossfade duration 0–10 seconds.
- Layer bone masks are derived once from joint ancestry; an empty mask means the full rig. Override and additive modes are evaluated only for authored/weighted bones.
- Notifies only fire for active blend sources and enabled layers. Non-looping clips fire once; looping markers are counted across the interval. Every tick limits output to 32 events and tracks overflow through `dropped_events()`.
- Events are available through the C++ graph instance and currently logged by Viewport. Routing them into the V8 `Events` bus is a separate pending integration.

## Asynchronous YCD clip residency

`AsyncYcdClipLoader` uses `AssetStore::request_bytes()` for asynchronous I/O, holds a shared immutable YMT rig, and schedules up to two (configurable 1–4) CPU decoder workers using `std::async`. Decoded clips are returned as `shared_ptr<SkeletalClip const>`. Polling never waits for a running task. A graph initially draws a bind pose until its clip data becomes resident, rather than stalling the render thread for YCD I/O or decoding.

The loader's destruction may wait for running decode tasks to finish; an explicit cancellation epoch, disk cache for decoded YCD, and cross-rig deduplication remain future improvements.

## QA

Compile with the repository's sole build root:

```powershell
cmake --build out/build --target northstarcpp_animation_graph_smoke northstarcpp_viewport_skeletal_pipeline_smoke
ctest --test-dir out/build --output-on-failure -R "northstarcpp_animation_graph|northstarcpp_viewport_skeletal_pipeline"
```

Graph smoke covers validated XML, Blend1D, crossfades, masked additive output, event triggering and suppression, and actual asynchronously decoded Abby YCD. The sample XML is packaged in `northstar_shared.nsp` (archive verifier PASS). The game Player built and linked successfully. Qt source modules compiled, but the Editor EXE link was blocked because Windows denied overwriting the existing running/locked `northstarcpp_editor_qt.exe`.

Automated QA: `northstarcpp_animation_graph`, `northstarcpp_skeletal_animation`, `northstarcpp_skeletal_asset_interop`, `northstarcpp_scene_animation`, `northstarcpp_asset_catalog` — **5/5 PASS**. Visual acceptance of live graph playback, transport/cutscene integrations, and GPU provider parity has not been completed.

The independent `northstarcpp_viewport_skeletal_pipeline` regression currently **fails** its strict 10 cm left-wrist-to-weapon-socket assertion. Weapon IK and self-collision are being modified in parallel; this remains unresolved. Do not weaken the acceptance threshold or claim complete regression parity.

## Future stages

- Visual Qt Animation Graph editor with typed ports, diagnostics and live pose inspector
- Event bridge to `engine.events` and root motion/physics integration
- 2D blend spaces, cached-pose DAG optimization, state machine subgraphs
- Fully asynchronous graph XML read, offline clip compression, decoded-data DDC
- Frame-time/p95 profiling on 100+ live character instances across Vulkan and D3D12
