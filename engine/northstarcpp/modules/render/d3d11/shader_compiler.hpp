#pragma once
// D3D11's compiler accepts native ID3DBlob** to avoid coupling to Renderer Com<T>.
#include <d3dcompiler.h>
namespace northstarcpp::render::d3d11::shader_compiler {
void compile(const char* entry,const char* profile,ID3DBlob** output);
void compile_sky(const char* entry,const char* profile,ID3DBlob** output);
void compile_character(const char* entry,const char* profile,ID3DBlob** output);
void compile_authored_material(const char* entry,const char* profile,ID3DBlob** output);
}
