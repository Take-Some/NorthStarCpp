#pragma once
// Private D3D12 shader compilation interface: no engine.render ABI changes.
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <string>
namespace northstarcpp::render::d3d12::shader_compiler {
Microsoft::WRL::ComPtr<ID3DBlob> compile(const char* entry);
Microsoft::WRL::ComPtr<ID3DBlob> compile_sky(const char* entry,const char* profile);
Microsoft::WRL::ComPtr<ID3DBlob> compile_editor_grid(
    const char* entry,const char* profile);
Microsoft::WRL::ComPtr<ID3DBlob> compile_authored_material(const char* entry,const char* profile);
Microsoft::WRL::ComPtr<ID3DBlob> compile_material_variant(
    const std::string& source,const char* entry,const char* profile);
Microsoft::WRL::ComPtr<ID3DBlob> compile_deferred(const char* entry,const char* profile);
Microsoft::WRL::ComPtr<ID3DBlob> compile_fxaa(const char* entry,const char* profile);
Microsoft::WRL::ComPtr<ID3DBlob> compile_bloom(const char* entry,const char* profile);
Microsoft::WRL::ComPtr<ID3DBlob> compile_hdr(const char* entry,const char* profile);
Microsoft::WRL::ComPtr<ID3DBlob> compile_shadow(const char* entry,const char* profile);
}
