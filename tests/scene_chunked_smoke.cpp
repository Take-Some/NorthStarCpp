// Chunked NEF8 random disk reads, plugin ECS blobs, and opaque preservation.
#include <northstarcpp/scene.hpp>
#include <northstarcpp/scene_chunk_index.hpp>
#include <northstarcpp/scene_component_registry.hpp>
#include <northstarcpp/scene_world.hpp>
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>
using namespace northstarcpp;
namespace {
void check(bool result,char const* error) {
    if(!result)throw std::runtime_error(error);
}
std::uint32_t c_validator(void*,std::uint32_t version,
                          void const* data,std::size_t bytes) {
    return version==1 && bytes==2 &&
           std::string(static_cast<char const*>(data),bytes)=="OK" ? 1u : 0u;
}
bool validator(void*,std::uint32_t version,void const* data,std::size_t bytes) {
    return version==3 && bytes>=4 &&
        std::string(static_cast<char const*>(data),4)=="rain";
}
template<class Fn> bool fails(Fn&& fn) {
    try {fn();}catch(std::exception const&){return true;}return false;
}
std::string read(std::filesystem::path const& path) {
    std::ifstream file(path,std::ios::binary);
    return {std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};
}
void write(std::filesystem::path const& path,std::string const& data) {
    std::ofstream file(path,std::ios::binary|std::ios::trunc);
    check(bool(file),"Cannot open test output");
    file.write(data.data(),static_cast<std::streamsize>(data.size()));
    check(bool(file),"Cannot write test output");
}
}
int main(int argc,char** argv) {
    if(argc!=2)return 2;
    auto path=std::filesystem::path(argv[1]);
    try {
        auto api=scene_component_host_api_v1();
        check(api && api->size>=sizeof(*api) &&
              api->abi==NORTHSTARCPP_SCENE_COMPONENT_HOST_ABI &&
              api->register_type && api->unregister_type,
              "C-compatible scene component host ABI is incomplete");
        check(api->register_type("com.kaylas.external",1,1,c_validator,nullptr),
              "C plugin cannot register its scene component schema");
        check(validate_scene_component_blob({0,"com.kaylas.external",1,"OK"}) &&
              !validate_scene_component_blob({0,"com.kaylas.external",1,"NO"}),
              "C plugin schema validator was not called");
        api->unregister_type("com.kaylas.external");
        check(validate_scene_component_blob({0,"com.kaylas.external",1,"NO"}),
              "Unloaded C plugin must preserve its opaque component data");

        Scene scene;
        scene.version=3;scene.name="Cell Streaming";
        scene.streaming.load_radius=6.f;
        scene.streaming.unload_radius=10.f;
        scene.cells.push_back({"A",{-50,-50,-50},{0,50,0},false});
        scene.cells.push_back({"B",{0,-50,0},{50,50,50},false});
        SceneObject persistent;
        persistent.name="Persistent";persistent.node.id=100;
        scene.objects.push_back(persistent);
        SceneObject a;
        a.name="District A";a.node.id=200;a.node.cell="A";a.position={-12,0,-5};
        scene.objects.push_back(a);
        SceneObject b;
        b.name="District B";b.node.id=300;b.node.cell="B";b.position={12,0,5};
        scene.objects.push_back(b);
        scene.components.push_back({200,"com.kaylas.weather",3,std::string("rain\0v3",7)});
        scene.components.push_back({300,"org.plugin.offline",92,std::string("\0\xfe\x01",3)});
        scene.opaque_records.push_back({400,2,300,std::string("future\0blob",11)});
        // A future version of a known record kind must be preserved too.
        scene.opaque_records.push_back({6,2,0,std::string("future-portal-v2")});

        check(register_scene_component_type({"com.kaylas.weather",3,3,
                                             validator,nullptr}),
              "Could not register component plugin descriptor");
        check(!register_scene_component_type({"com.kaylas.weather",3,3,
                                              validator,nullptr}),
              "Duplicate ECS provider registration accepted");
        validate_scene(scene);
        auto encoded=serialize_scene_binary(scene);
        check(encoded.compare(0,4,"NEF8")==0 &&
              static_cast<unsigned char>(encoded[10])==2 &&
              static_cast<unsigned char>(encoded[12])==2,
              "Scene should be NEF8 v2 chunked");
        write(path,encoded);
        auto index=index_scene_chunks(path);
        check(index.size()==3 && index[0].cell.empty() &&
              index[1].cell=="A" && index[2].cell=="B",
              "Scene chunk index lacks independently addressable cells");
        check(index[1].file_offset+index[1].compressed_bytes==
              index[2].file_offset,
              "Cell chunk ranges overlap or contain gaps");
        auto only_a=load_scene_chunk(path,"A");
        auto only_b=load_scene_chunk(path,"B");
        auto global=load_scene_chunk(path,"");
        check(only_a.size()==2 && only_b.size()==3 && global.size()==5,
              "Scene cell component chunk grouping mismatch");
        check(only_a.front().entity_id==200 && only_b.front().entity_id==300,
              "Direct cell read returned wrong entity");
        check(load_scene_chunk(path,"unassigned").empty(),
              "Unknown streaming cell should return empty records");
        // SceneWorld can request/evict independently compressed cells.
        SceneWorld world(scene);
        SceneCellDiskResidency on_disk(path);
        check(on_disk.loaded("") && on_disk.resident_cell_count()==1,
              "Only the global chunk should be loaded initially");
        world.update({-20.f,0.f,-20.f});
        on_disk.apply(world);
        check(on_disk.loaded("A") && !on_disk.loaded("B"),
              "Disk streaming fetched an unwanted cell");
        world.update({25.f,0.f,25.f});
        on_disk.apply(world);
        check(!on_disk.loaded("A") && on_disk.loaded("B"),
              "Disk streaming failed to transition between cells");
        check(on_disk.records("B") && on_disk.records("B")->size()==3,
              "Cell B did not preserve its ECS records");
        auto decoded=load_scene(path);
        check(decoded.objects.size()==3 && decoded.components.size()==2 &&
              decoded.opaque_records.size()==2,
              "ECS data lost on decoding chunked scene");
        check(decoded.components[0].payload==scene.components[0].payload &&
              decoded.components[1].payload==scene.components[1].payload &&
              decoded.opaque_records[0].payload==scene.opaque_records[0].payload &&
              decoded.opaque_records[0].version==2 &&
              decoded.opaque_records[1].kind==6 &&
              decoded.opaque_records[1].version==2 &&
              decoded.opaque_records[1].payload=="future-portal-v2",
              "Opaque plugin component bytes/version changed");
        check(serialize_scene_binary(decoded)==encoded,
              "Chunked NEF8 scene roundtrip not deterministic");
        unregister_scene_component_type("com.kaylas.weather");
        // The unregistered provider must not prevent reading/writing its data.
        check(serialize_scene_binary(parse_scene_binary(encoded))==encoded,
              "Unloaded plugin destroyed serialized ECS components");

        // Re-register plugin and verify it rejects invalid supported versions.
        check(register_scene_component_type({"com.kaylas.weather",3,3,
                                             validator,nullptr}),
              "Cannot register ECS provider after unloading");
        auto bad=scene;
        bad.components[0].payload="hail";
        check(fails([&]{serialize_scene_binary(bad);}),
              "Registered component validator was not invoked");
        bad=scene;bad.components[0].version=4;
        check(!fails([&]{serialize_scene_binary(bad);}),
              "Unknown future component version should remain opaque");
        unregister_scene_component_type("com.kaylas.weather");

        // Damage only the B payload; a disk read of A remains valid.
        auto damaged=encoded;
        auto const b_offset=static_cast<std::size_t>(index[2].file_offset);
        damaged[b_offset]^=0x55;
        write(path,damaged);
        check(load_scene_chunk(path,"A").size()==2,
              "Unrelated cell was forced to decompress a corrupt neighbor");
        check(fails([&]{load_scene_chunk(path,"B");}),
              "Corrupt cell failed to trigger checksum validation");
        check(fails([&]{load_scene(path);}),
              "Full decode failed to detect bad chunk");
        write(path,encoded);
        auto corrupt_index=encoded;
        corrupt_index[128+16+56]^=0x77;
        write(path,corrupt_index);
        check(fails([&]{index_scene_chunks(path);}),
              "Tampered chunk metadata was accepted");
        std::error_code ec;
        std::filesystem::remove(path,ec);
        return 0;
    } catch(std::exception const& error) {
        std::fprintf(stderr,"Scene chunked smoke: %s\n",error.what());
        return 1;
    }
}
