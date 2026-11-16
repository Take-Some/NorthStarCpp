// Real LoadLibrary + Module::resolve + ECS reflected registration/unload QA.
// The fixture exposes a minimal engine.render API solely to exercise normal
// Module provider selection; no GPU device is created by this test.
#include "module_loader.hpp"
#include <northstarcpp/scene_component_registry.hpp>
#include <northstarcpp/render_plugin.h>
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <stdexcept>
#include <string>
#ifdef NORTHSTARCPP_SCENE_COMPONENT_STANDALONE_TEST
// Isolated loader integration test can link without the unrelated physics
// runtime. The production/CMake test uses the real builtin module catalog.
namespace northstarcpp {
extern BuiltinModule const builtin_modules[]={{nullptr,nullptr}};
}
#endif
using namespace northstarcpp;
namespace {
void require(bool condition,char const* message) {
    if(!condition)throw std::runtime_error(message);
}
}
int main(int argc,char** argv) {
    if(argc!=2)return 2;
    HMODULE retained=nullptr;
    try {
        auto const file=std::filesystem::absolute(
            std::filesystem::u8path(argv[1]));
        require(std::filesystem::is_regular_file(file),"DLL fixture missing");
        require(SetEnvironmentVariableW(L"NORTHSTARCPP_RENDER_PLUGIN",
                                        file.c_str())!=0,
                "Failed to set exact provider DLL override");
        retained=LoadLibraryExW(file.c_str(),nullptr,
            LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|
            LOAD_LIBRARY_SEARCH_APPLICATION_DIR|
            LOAD_LIBRARY_SEARCH_SYSTEM32);
        require(retained,"Unable to LoadLibrary the fixture");
        auto fail=reinterpret_cast<void(*)(int)>(
            GetProcAddress(retained,"northstarcpp_scene_fixture_fail"));
        auto count=reinterpret_cast<unsigned(*)()>(
            GetProcAddress(retained,"northstarcpp_scene_fixture_calls"));
        require(fail && count,"Fixture exported registration diagnostics missing");
        fail(1);
        bool rejected=false;
        try {
            Module provider("engine.render","northstar_ecs_reflection_qa");
            provider.query<NorthStarRenderApi>(NORTHSTARCPP_RENDER_ABI);
        }catch(std::exception const&) {rejected=true;}
        require(rejected,"Failed DLL registration did not reject provider");
        require(!scene_component_schema("com.northstar.qa.reflected_dll"),
                "Failed callback left dangling plugin callbacks");
        require(count()==1,"Expected one rejected callback");
        fail(0);
        SceneComponentBlob blob{
            1,"com.northstar.qa.reflected_dll",1,std::string(8,'\0')};
        {
            Module primary("engine.render","northstar_ecs_reflection_qa");
            require(primary.query<NorthStarRenderApi>(NORTHSTARCPP_RENDER_ABI),
                    "Provider module query failed");
            auto schema=scene_component_schema(blob.type);
            require(schema && schema->editor_fields.size()==2,
                    "Selected DLL reflection was not registered");
            require(write_scene_component_field(
                        blob,schema->editor_fields[0],2.5f) &&
                    write_scene_component_field(
                        blob,schema->editor_fields[1],std::uint32_t(9)) &&
                    validate_scene_component_blob(blob),
                    "Registered DLL validator or field editor failed");
            {
                Module secondary("engine.render","northstar_ecs_reflection_qa");
                require(secondary.query<NorthStarRenderApi>(NORTHSTARCPP_RENDER_ABI),
                        "Second service could not share external DLL");
                require(scene_component_schema(blob.type).has_value(),
                        "Shared DLL registration unexpectedly disappeared");
                require(count()==2,"Shared DLL callback ran more than once");
            }
            require(scene_component_schema(blob.type).has_value(),
                    "First DLL reference lost metadata after second unloaded");
        }
        require(!scene_component_schema(blob.type),
                "Final DLL reference failed to unregister metadata");
        require(validate_scene_component_blob(blob),
                "Unloaded plugin payload did not survive as opaque bytes");
        {
            Module reloaded("engine.render","northstar_ecs_reflection_qa");
            require(reloaded.query<NorthStarRenderApi>(NORTHSTARCPP_RENDER_ABI),
                    "DLL could not re-register after unload");
            require(count()==3 && scene_component_schema(blob.type),
                    "Reloaded DLL reflection is stale");
        }
        require(!scene_component_schema(blob.type),
                "Reloaded DLL did not unregister");
        FreeLibrary(retained);retained=nullptr;
        SetEnvironmentVariableW(L"NORTHSTARCPP_RENDER_PLUGIN",nullptr);
        std::puts("PASS: selected DLL reflection, rollback, refcounts, unload, reload");
        return 0;
    } catch(std::exception const& e) {
        std::fprintf(stderr,"DLL ECS lifecycle smoke: %s\n",e.what());
        if(retained)FreeLibrary(retained);
        SetEnvironmentVariableW(L"NORTHSTARCPP_RENDER_PLUGIN",nullptr);
        return 1;
    }
}
