# NorthStarCpp GPU Picking v2 — ECS Entity IDs, Vulkan/D3D12
Date: 2026-11-16
QA build: _qa_editor_mixed

## Implemented
- Additive NORTHSTARCPP_RENDER_PICK_V2_MODULE_ABI 0x52504b02; stable 64-bit ECS Entity IDs; existing render ABI 5 and v1 retained.
- World-space triangle stream uses independent per-triangle IDs, so YDD/YDR sections grouped by one YMAT are still individually selectable.
- Vulkan and D3D12 convert stable IDs to transient 24-bit GPU colors and recover the original ID after depth-tested RGBA8 readback.
- Qt viewport resolves returned stable Entity ID to the current scene actor and falls back to geometry raycast for unsupported/incomplete scenes.
- World positions are generated for normal OBJ/YDD/YDR materials, untextured/procedural meshes, character markers, and GPU-skinned batches (CPU mirror of active bone palette).
- Editor pick geometry capped at 180,000 vertices; no silent truncation. Skipped/transparent materials disable ID picking to avoid ghost selection.

## Hardware acceptance
NVIDIA Quadro RTX 4000, non-software device. Both Vulkan and D3D12 GPU provider probes PASS:
64-bit overlapping cube IDs; nearest of two world-space mesh triangles in front of a cube; empty background;
simulated pose shift that reveals the cube. Imported Night City YDD/YDR + real YCD animation not yet verified end-to-end.

CTest final: 9/9 PASS (42.42 s):
D3D11 hardware; D3D12 stress 1000 frames + hardware; Vulkan stress 1000 frames + hardware;
Qt authoring; Qt GPU surface; PIE render; ECS DLL lifecycle.
Qt Editor and Player Release link PASS. git diff --check exit 0.

## Outstanding
- GPU-native skinning in ID pass (current world-space picking positions are CPU-deformed).
- Alpha-mask/transparent material depth semantics; complex shaders.
- Persistent ID attachments and nonblocking double/triple-buffered readback: currently on-demand GPU fence wait.
- Device-lost, hot reload, repeated picking + resize endurance.
- Real Night City YDD/YDR/YCD click-selection acceptance and performance p95/p99.
No production binary replacement, Git commit or push.
