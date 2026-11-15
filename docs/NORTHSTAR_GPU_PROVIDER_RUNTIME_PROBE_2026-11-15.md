# NorthStarCpp: Real GPU Provider Device Validation

Date: 2026-11-15
Core Render ABI: 5 (unchanged)
Host module ABI: 1 (unchanged)

## Deliverables

The Project Browser now offers a user-triggered **Test on This GPU** action in the renderer settings. It launches the standalone northstarcpp_gpu_provider_probe.exe in a separate, monitored QProcess. No GPU is initialized while scanning projects. The UI remains responsive; a GPU probe that hangs for 45 seconds is terminated. Errors and process crashes are reported without falling back to another renderer.

Source: tests/render_provider_device_probe.cpp
Target: northstarcpp_gpu_provider_probe
Executable: out/project-manager-qt/northstarcpp_gpu_provider_probe.exe

Command: northstarcpp_gpu_provider_probe.exe <full DLL path> --full

The executable loads exactly the supplied external DLL, validates the provider's Render ABI 5, creates a real Win32 renderer with hardware requested, submits colored geometry and verifies pixels read back from GPU. Where supported, it uploads an 8x8 red/green texture (source bytes are overwritten after upload), renders a quad and verifies both sampled colors via GPU readback. It submits a programmable Material Graph v2 variant and waits for the asynchronous PSO compilation to produce a green GPU result. It also requests an optional valid GPU timestamp after rendering.

The program prints one JSON report containing status, device initialization, tested and unsupported feature paths, device identity and timing. Missing or incompatible DLLs return a nonzero exit code and descriptive JSON.

## Device identity from the active renderer

New optional diagnostic extension:
engine/northstarcpp/modules/api/include/northstarcpp/render_device_api.h

Extension: NORTHSTARCPP_RENDER_DEVICE_MODULE_ABI = 0x52445601. It uses a versioned fixed-size C POD. Existing Render ABI 5 and module ABI are unchanged.

D3D11 queries IDXGIDevice and its active IDXGIAdapter1.
D3D12 queries ID3D12Device::GetAdapterLuid and the corresponding IDXGIAdapter1.
Vulkan reports the actual VkPhysicalDevice selected by the renderer.

The output includes GPU name, Vendor/Device IDs, backend, API version and software/hardware flag. If the extension is missing, the GPU name is unknown; an unrelated enumerated adapter is NOT represented as the actual GPU.

## Hardware QA results

Actual GPU for all three isolated QA provider DLLs:
NVIDIA GeForce GTX 750; Vendor ID 0x10DE; Device ID 0x1407. All report software_adapter=false.

| Runtime check | D3D11 | D3D12 | Vulkan |
| --- | --- | --- | --- |
| Hardware renderer creation | PASS | PASS | PASS |
| Actual GPU adapter identity | PASS | PASS | PASS |
| GPU geometry pixel readback | PASS | PASS | PASS |
| Texture UV GPU readback | PASS | PASS | Unsupported API |
| Material Graph GPU tint verification | PASS | PASS | Unsupported API |
| GPU timing extension | Unavailable | PASS | PASS |

Verified new DLL builds:
out/project-manager-qt/device-qa/render_d3d11.dll
out/project-manager-qt/device-qa/render_d3d12.dll
out/project-manager-qt/device-qa/render_vulkan.dll

The binaries were linked with fresh compiled objects to separate filenames because the conventional DLL output files were locked by other Windows processes. Production dist remains unchanged.

## GUI and regression

The Project Browser button is asynchronous. It displays adapter name and IDs, geometry and texture GPU verification, Material Graph status and timing, with an explicit warning that HDR, bloom and shadow quality are NOT verified by this test. Renderer selection and missing provider guards remain strict.

Updated GUI from current source was linked to:
out/project-manager-qt/qa_project_manager_gpu_device.exe

This executable passed --smoke (exit 0), including an assertion that the hardware test button and results label are available in the settings dialog.

The selected CTest suite reported **11/11 passed**, covering three real GPU provider smoke tests, the official GUI/catalog and provider-selection tests. Negative manual tests passed: an absent DLL and a DLL for the wrong API both produced a structured error and nonzero exit.

The standard northstarcpp_project_manager.exe also exists and passed the official GUI smoke. Its last relink was blocked by an active Windows file lock, so the latest UI revision was additionally verified through the separate QA executable.

## Scope and remaining work

These checks exercise real GPU creation, geometry, texture sampling, programmable material graph behavior and timestamps. They are not proof of full Vulkan feature parity, HDR quality, CSM/PCSS accuracy, bloom, exposure, SSAO, or Night City frame-time performance.

Next steps: dedicated GPU reference tests for every advanced feature; builtin-only provider probing; optional project-specific mandatory-feature requirements; cache results only when keyed to the exact DLL and GPU/driver; Vulkan PBR parity; actual Night City benchmarks.

No production dist replacement, Git commit or push. Existing unrelated working-tree changes preserved.
