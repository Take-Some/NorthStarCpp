// Runtime GPU adapter identity, queried from the actual renderer instance.
// Optional to engine.render ABI 5, no layout changes to existing contracts.
#pragma once
#include <northstarcpp/module.h>
#include <stdint.h>
#define NORTHSTARCPP_RENDER_DEVICE_ABI 1u
#define NORTHSTARCPP_RENDER_DEVICE_MODULE_ABI 0x52445601u
#define NORTHSTARCPP_RENDER_DEVICE_D3D11 11u
#define NORTHSTARCPP_RENDER_DEVICE_D3D12 12u
#define NORTHSTARCPP_RENDER_DEVICE_VULKAN 13u
#ifdef __cplusplus
extern "C" {
#endif
typedef struct NorthStarRenderDeviceInfo {
    uint32_t abi_version;
    uint32_t struct_size;
    uint32_t backend;
    uint32_t software;
    uint32_t vendor_id;
    uint32_t device_id;
    uint32_t api_version;
    uint32_t reserved;
    char adapter_name_utf8[128];
} NorthStarRenderDeviceInfo;
typedef struct NorthStarRenderDeviceApi {
    uint32_t abi_version;
    int (*read)(void* renderer,NorthStarRenderDeviceInfo* result);
} NorthStarRenderDeviceApi;
#ifdef __cplusplus
}
static_assert(sizeof(NorthStarRenderDeviceInfo)==160,"GPU device ABI layout changed");
#endif
