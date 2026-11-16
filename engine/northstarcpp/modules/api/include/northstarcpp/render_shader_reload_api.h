// NorthStar Engine optional shader-reload provider extension.
// Render ABI 5 is unchanged. Provider owns all GPU objects and copies input bytes.
// All calls except GPU pipeline construction occur on the render/host thread.
// "ready" means the provider has installed a new PSO after a completed frame fence.
#pragma once
#include <northstarcpp/render_materials_api.h>
#ifdef __cplusplus
extern "C" {
#endif
#define NORTHSTARCPP_RENDER_SHADER_RELOAD_ABI 1u
#define NORTHSTARCPP_RENDER_SHADER_RELOAD_MODULE_ABI 0x52534801u
#define NORTHSTARCPP_SHADER_RELOAD_MAX_SPIRV_WORDS 262144u
#define NORTHSTARCPP_SHADER_RELOAD_MAX_BATCHES NORTHSTARCPP_RENDER_MATERIAL_MAX_BATCHES
enum NorthStarShaderReloadState {
    NS_SHADER_RELOAD_UNKNOWN=0u,
    NS_SHADER_RELOAD_QUEUED=1u,
    NS_SHADER_RELOAD_BUILDING=2u,
    NS_SHADER_RELOAD_READY=3u,
    NS_SHADER_RELOAD_FAILED=4u
};
typedef struct NorthStarShaderReloadStatus {
    uint32_t struct_size; // caller fills sizeof(); implementation checks it
    uint32_t state;
    uint64_t revision; // last requested revision
    uint64_t installed_revision; // last fully installed pipeline revision
    char diagnostic[256]; // null-terminated (if failed), without shared pointers
} NorthStarShaderReloadStatus;
typedef struct NorthStarRenderShaderReloadApi {
    uint32_t abi_version;
    uint32_t struct_size;
    // Batch association is positional to the most recently submitted material
    // buffers (materials v1 or v2); each key is a virtual material reference.
    // Provider copies the strings. count=0 clears associations.
    int (*set_material_references)(void* renderer,
        const char* const* references,uint32_t count);
    // Asynchronous bounded PSO build for one existing material reference.
    // Fragment SPIR-V uses the current material vertex ABI and descriptor layout.
    // Only a validated forward material pipeline can be replaced.
    // No filesystem access and no arbitrary DLL loading inside the provider.
    int (*queue_fragment_spirv)(void* renderer,const char* material_reference,
        const uint32_t* spirv,uint32_t word_count);
    // Nonblocking poll; call each editor tick until READY/FAILED.
    int (*poll)(void* renderer,const char* material_reference,
        NorthStarShaderReloadStatus* status);
} NorthStarRenderShaderReloadApi;

// Shader Reload ABI v2 is separate from v1: previously installed external
// providers still expose the same original ABI table without requiring recompilation.
#define NORTHSTARCPP_RENDER_SHADER_RELOAD_V2_ABI 2u
#define NORTHSTARCPP_RENDER_SHADER_RELOAD_V2_MODULE_ABI 0x52534802u
#define NORTHSTARCPP_SHADER_RELOAD_MAX_BINARY_BYTES (1024u*1024u)
enum NorthStarShaderBinaryFormat {
    NS_SHADER_BINARY_SPIRV=1u,
    NS_SHADER_BINARY_DXIL=2u
};
enum NorthStarShaderProgramStage {
    NS_SHADER_PROGRAM_VERTEX=1u,
    NS_SHADER_PROGRAM_FRAGMENT_FORWARD=2u,
    NS_SHADER_PROGRAM_FRAGMENT_GBUFFER=3u
};
typedef struct NorthStarRenderShaderReloadApiV2 {
    uint32_t abi_version;
    uint32_t struct_size;
    int (*set_material_references)(void* renderer,
        const char* const* references,uint32_t count);
    // Stage-specific program replacement. Bytecode is copied by the provider.
    // Complete affected PSOs are built asynchronously, then atomically swapped
    // after the provider's completed-frame fence. Never mix SPIR-V and DXIL.
    int (*queue_shader_binary)(void* renderer,const char* material_reference,
        uint32_t stage,uint32_t format,const uint8_t* bytes,uint32_t size);
    int (*poll)(void* renderer,const char* material_reference,
        NorthStarShaderReloadStatus* status);
} NorthStarRenderShaderReloadApiV2;
#ifdef __cplusplus
}
#endif
