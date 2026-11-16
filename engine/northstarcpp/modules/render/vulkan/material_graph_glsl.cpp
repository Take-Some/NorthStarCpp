// NorthStarCpp Vulkan Material Graph ABI v2.
// Reuses the shared HLSL expression DSL, maps its whitelisted operators to
// GLSL, then compiles real SPIR-V through the Vulkan SDK shaderc runtime.
// The DLL is loaded lazily so plain Vulkan materials retain no shaderc dep.
#include "material_graph_glsl.hpp"
#include "../shared/material_variants.hpp"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shaderc/shaderc.h>
#include <array>
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>
#include <type_traits>

namespace northstarcpp::render::vulkan {
namespace {
void replace_all(std::string& text,const std::string& from,
                 const std::string& into) {
    std::size_t start=0;
    while((start=text.find(from,start))!=std::string::npos) {
        text.replace(start,from.size(),into);
        start+=into.size();
    }
}
std::string convert_identifier_tokens(const std::string& input) {
    std::string out;
    out.reserve(input.size()+64);
    for(std::size_t start=0;start<input.size();) {
        const unsigned char c=static_cast<unsigned char>(input[start]);
        const bool alpha=(c>='a'&&c<='z')||(c>='A'&&c<='Z')||c=='_';
        if(!alpha) {out+=input[start++];continue;}
        auto end=start+1;
        while(end<input.size()) {
            const unsigned char n=static_cast<unsigned char>(input[end]);
            if(!((n>='a'&&n<='z')||(n>='A'&&n<='Z')||
                 (n>='0'&&n<='9')||n=='_'))break;
            ++end;
        }
        const std::string token=input.substr(start,end-start);
        if(token=="float4")out+="vec4";
        else if(token=="float3")out+="vec3";
        else if(token=="float2")out+="vec2";
        else if(token=="lerp")out+="mix";
        else out+=token;
        start=end;
    }
    return out;
}
struct ShadercLibrary {
    HMODULE module=nullptr;
    decltype(&shaderc_compiler_initialize) initialize=nullptr;
    decltype(&shaderc_compiler_release) release_compiler=nullptr;
    decltype(&shaderc_compile_options_initialize) options_initialize=nullptr;
    decltype(&shaderc_compile_options_release) options_release=nullptr;
    decltype(&shaderc_compile_options_set_target_env) set_target=nullptr;
    decltype(&shaderc_compile_options_set_optimization_level) set_optimization=nullptr;
    decltype(&shaderc_compile_into_spv) compile=nullptr;
    decltype(&shaderc_result_get_compilation_status) status=nullptr;
    decltype(&shaderc_result_get_length) length=nullptr;
    decltype(&shaderc_result_get_bytes) bytes=nullptr;
    decltype(&shaderc_result_get_error_message) message=nullptr;
    decltype(&shaderc_result_release) release_result=nullptr;
    ShadercLibrary() {
        // Production providers can live in /plugins, not the executable
        // directory. Resolve beside THIS provider DLL before SDK fallback.
        HMODULE owner=nullptr;
        if(GetModuleHandleExW(
               GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|
               GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
               reinterpret_cast<LPCWSTR>(&compile_material_fragment_spirv),
               &owner)) {
            std::array<wchar_t,32768> module_path{};
            const auto length=GetModuleFileNameW(
                owner,module_path.data(),static_cast<DWORD>(module_path.size()));
            if(length>0 && length<module_path.size()) {
                auto file=std::wstring(module_path.data(),length);
                if(auto const separator=file.find_last_of(L"\\/");
                   separator!=std::wstring::npos) {
                    file.resize(separator+1);
                    file+=L"shaderc_shared.dll";
                    module=LoadLibraryExW(file.c_str(),nullptr,
                        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|
                        LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
                    if(!module) {
                        file.resize(separator+1);
                        file+=L"libshaderc_shared.dll";
                        module=LoadLibraryExW(file.c_str(),nullptr,
                            LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|
                            LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
                    }
                }
            }
        }
        if(!module)module=LoadLibraryW(L"shaderc_shared.dll");
        if(!module)module=LoadLibraryW(L"libshaderc_shared.dll");
        if(!module) {
            std::array<wchar_t,2048> sdk{};
            auto n=GetEnvironmentVariableW(L"VULKAN_SDK",sdk.data(),
                                          static_cast<DWORD>(sdk.size()));
            if(n>0&&n<sdk.size()) {
                auto path=std::wstring(sdk.data())+L"\\Bin\\shaderc_shared.dll";
                module=LoadLibraryW(path.c_str());
                if(!module) {
                    path=std::wstring(sdk.data())+L"\\Bin\\libshaderc_shared.dll";
                    module=LoadLibraryW(path.c_str());
                }
            }
        }
        if(!module)
            throw std::runtime_error(
                "Vulkan Material Graph v2 requires shaderc_shared.dll or libshaderc_shared.dll "
                "(install the Vulkan SDK or package its runtime library)");
        const auto bind=[&](auto& field,const char* name) {
            field=reinterpret_cast<std::remove_reference_t<decltype(field)>>(
                GetProcAddress(module,name));
            if(!field)throw std::runtime_error(
                std::string("Incomplete shaderc_shared.dll: ")+name);
        };
        try {
            bind(initialize,"shaderc_compiler_initialize");
            bind(release_compiler,"shaderc_compiler_release");
            bind(options_initialize,"shaderc_compile_options_initialize");
            bind(options_release,"shaderc_compile_options_release");
            bind(set_target,"shaderc_compile_options_set_target_env");
            bind(set_optimization,"shaderc_compile_options_set_optimization_level");
            bind(compile,"shaderc_compile_into_spv");
            bind(status,"shaderc_result_get_compilation_status");
            bind(length,"shaderc_result_get_length");
            bind(bytes,"shaderc_result_get_bytes");
            bind(message,"shaderc_result_get_error_message");
            bind(release_result,"shaderc_result_release");
        }catch(...) {FreeLibrary(module);module=nullptr;throw;}
    }
    ~ShadercLibrary() {if(module)FreeLibrary(module);}
    ShadercLibrary(const ShadercLibrary&)=delete;
    ShadercLibrary& operator=(const ShadercLibrary&)=delete;
};
}
std::string validated_material_graph_glsl(
    const char* source,std::uint32_t size,
    std::uint32_t blend_mode,float alpha_cutoff)
{
    // Shared whitelist/size/policy validation also used by D3D11/D3D12.
    auto prefix=northstarcpp::shaders::material_shader_prefix(
        source,size,blend_mode,alpha_cutoff);
    // Whitelisted compiler expressions contain only these GPU sampler calls.
    // Map exact spellings, not arbitrary C/HLSL content or user code.
    static constexpr std::array<const char*,5> sampled{{
        "base_map.Sample(material_sampler,",
        "normal_map.Sample(material_sampler,",
        "rough_map.Sample(material_sampler,",
        "metal_map.Sample(material_sampler,",
        "emissive_map.Sample(material_sampler,"}};
    static constexpr std::array<const char*,5> glsl{{
        "texture(albedo_sampler,",
        "texture(normal_sampler,",
        "texture(roughness_sampler,",
        "texture(metallic_sampler,",
        "texture(emissive_sampler,"}};
    for(unsigned i=0;i<sampled.size();++i)
        replace_all(prefix,sampled[i],glsl[i]);
    return convert_identifier_tokens(prefix);
}
std::vector<std::uint32_t> compile_material_fragment_spirv(
    const std::string& shader_source)
{
    if(shader_source.empty()||shader_source.size()>48*1024)
        throw std::invalid_argument("Material Graph GLSL source exceeds 48 KiB");
    ShadercLibrary shaderc;
    auto compiler=shaderc.initialize();
    if(!compiler)throw std::runtime_error("Failed to initialize Vulkan shaderc");
    auto options=shaderc.options_initialize();
    if(!options) {
        shaderc.release_compiler(compiler);
        throw std::runtime_error("Failed to initialize shaderc options");
    }
    try {
        shaderc.set_target(options,shaderc_target_env_vulkan,
                           shaderc_env_version_vulkan_1_3);
        shaderc.set_optimization(options,shaderc_optimization_level_performance);
        auto result=shaderc.compile(compiler,shader_source.data(),
            shader_source.size(),shaderc_glsl_fragment_shader,
            "northstar-material-graph.frag","main",options);
        if(!result)throw std::runtime_error("shaderc returned no SPIR-V result");
        try {
            if(shaderc.status(result)!=shaderc_compilation_status_success) {
                auto diagnostics=shaderc.message(result);
                throw std::runtime_error(std::string(
                    "Vulkan Material Graph GLSL validation failed: ")+
                    (diagnostics?diagnostics:"unknown shaderc error"));
            }
            const auto length=shaderc.length(result);
            auto data=shaderc.bytes(result);
            if(!data||length<20||length%4!=0)
                throw std::runtime_error("shaderc returned malformed SPIR-V");
            std::vector<std::uint32_t> spirv(length/4);
            std::memcpy(spirv.data(),data,length);
            if(spirv[0]!=0x07230203)
                throw std::runtime_error("shaderc SPIR-V magic mismatch");
            shaderc.release_result(result);
            shaderc.options_release(options);
            shaderc.release_compiler(compiler);
            return spirv;
        }catch(...) {shaderc.release_result(result);throw;}
    }catch(...) {
        shaderc.options_release(options);
        shaderc.release_compiler(compiler);
        throw;
    }
}
} // namespace northstarcpp::render::vulkan
