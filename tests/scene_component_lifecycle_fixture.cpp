// Native DLL fixture for scene-component registration lifecycle QA.
// Intentionally no dependency on the engine's C++ runtime.
#include <northstarcpp/module.h>
#include <northstarcpp/render_plugin.h>
#include <northstarcpp/scene_component_plugin.h>
#include <cstring>
#include <cstdint>

namespace {
unsigned registered_calls=0;
bool fail_registration=false;
std::uint32_t validate(void*,std::uint32_t version,
                       void const*,std::size_t bytes) {
    return version==1 && bytes==8 ? 1u : 0u;
}
void* create(void*,std::uint32_t) {return reinterpret_cast<void*>(1);}
void destroy(void*) {}
int draw(void*,NorthStarRenderFrame const*) {return 1;}
int set_sky(void*,NorthStarSky const*) {return 1;}
const char* last_error() {return "";}
const NorthStarRenderApi render_api{create,destroy,draw,set_sky};
const NorthStarModule module{
    sizeof(NorthStarModule),NORTHSTARCPP_MODULE_ABI,
    NORTHSTARCPP_CAPABILITY_RENDER,
    "northstar-ecs-reflection-qa","1.0.0",NORTHSTARCPP_RENDER_ABI,
    sizeof(render_api),&render_api,last_error
};
}
extern "C" NORTHSTARCPP_MODULE_EXPORT const NorthStarModule*
northstar_module_query(std::uint32_t host,
    char const* capability,std::uint32_t version) {
    return host==NORTHSTARCPP_MODULE_ABI &&
        version==NORTHSTARCPP_RENDER_ABI &&
        capability &&
        std::strcmp(capability,NORTHSTARCPP_CAPABILITY_RENDER)==0
        ?&module:nullptr;
}
extern "C" NORTHSTARCPP_MODULE_EXPORT std::uint32_t
northstarcpp_register_scene_components_v1(
    NorthStarSceneComponentHostApi const* host) {
    ++registered_calls;
    if(!host || host->abi!=NORTHSTARCPP_SCENE_COMPONENT_HOST_ABI ||
       host->size<offsetof(NorthStarSceneComponentHostApi,
           register_type_reflected)+sizeof(host->register_type_reflected) ||
       !host->register_type_reflected)
        return 0;
    NorthStarSceneComponentFieldC fields[]{
        {"Current",NORTHSTARCPP_SCENE_COMPONENT_FIELD_F32,0},
        {"Flags",NORTHSTARCPP_SCENE_COMPONENT_FIELD_U32,4}
    };
    if(!host->register_type_reflected("com.northstar.qa.reflected_dll",
                                     1,1,validate,nullptr,fields,2))
        return 0;
    // Deliberate failure after registration tests host-owned rollback.
    return fail_registration?0u:1u;
}
extern "C" NORTHSTARCPP_MODULE_EXPORT unsigned
northstarcpp_scene_fixture_calls() {return registered_calls;}
extern "C" NORTHSTARCPP_MODULE_EXPORT void
northstarcpp_scene_fixture_fail(int fail) {fail_registration=fail!=0;}
