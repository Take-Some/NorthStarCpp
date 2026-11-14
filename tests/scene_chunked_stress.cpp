// Indexed NSCN v2 chunk stress: many cells without full-scene inflation.
#include <northstarcpp/scene.hpp>
#include <northstarcpp/scene_chunk_index.hpp>
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>
#include <iostream>
namespace {
void check(bool good,char const* message) {
    if(!good)throw std::runtime_error(message);
}
}
int main(int argc,char** argv) {
    if(argc!=2)return 2;
    try {
        northstarcpp::Scene scene;
        scene.version=3;scene.name="Chunk Index Stress";
        constexpr unsigned columns=32, rows=24;
        for(unsigned row=0;row<rows;++row) {
            for(unsigned col=0;col<columns;++col) {
                std::string name="cell_"+std::to_string(row)+"_"+std::to_string(col);
                float x=float(col*24),z=float(row*24);
                scene.cells.push_back({name,{x,0,z},{x+23,30,z+23},false});
                northstarcpp::SceneObject object;
                object.name="Cell Object";
                object.node.id=std::uint64_t(row*columns+col+1);
                object.node.cell=name;
                object.position={x+12.f,3.f,z+12.f};
                scene.objects.push_back(object);
                // Repetitive but nontrivial data — each cell remains independently
                // DEFLATE compressed and can be fetched by name.
                std::string payload(256,'A');
                payload.replace(0,std::min(name.size(),payload.size()),name);
                scene.components.push_back({
                    object.node.id,"org.northstar.stress",1,payload});
            }
        }
        northstarcpp::validate_scene(scene);
        auto path=std::filesystem::path(argv[1]);
        northstarcpp::save_scene(scene,path);
        auto chunks=northstarcpp::index_scene_chunks(path);
        check(chunks.size()==columns*rows+1,"Missing Scene streaming chunks");
        std::uint64_t expanded=0,read_expanded=0;
        for(auto const& chunk:chunks)expanded+=chunk.expanded_bytes;
        for(unsigned i=0;i<24;++i) {
            auto row=(i*11)%rows,col=(i*17)%columns;
            auto name="cell_"+std::to_string(row)+"_"+std::to_string(col);
            auto at=std::find_if(chunks.begin(),chunks.end(),
                [&](auto const& entry){return entry.cell==name;});
            check(at!=chunks.end(),"Cell absent from chunk index");
            auto records=northstarcpp::load_scene_chunk(path,name);
            check(records.size()==2 &&
                  records[0].entity_id==std::uint64_t(row*columns+col+1),
                  "Disk cell did not produce correct entity and ECS blob");
            read_expanded+=at->expanded_bytes;
        }
        check(read_expanded*5 < expanded,
              "Sparse reads expanded too much of the world");
        auto restored=northstarcpp::load_scene(path);
        check(restored.objects.size()==columns*rows &&
              restored.components.size()==columns*rows &&
              northstarcpp::serialize_scene_binary(restored)==
                  northstarcpp::serialize_scene_binary(scene),
              "Large world round-trip lost ECS components");
        std::error_code error;
        std::filesystem::remove(path,error);
        std::cout<<"Stress: "<<columns*rows<<" cells, 24 sampled; selective expanded "
                 <<read_expanded<<" / "<<expanded<<" bytes\n";
        return 0;
    } catch(std::exception const& ex) {
        std::cerr<<"Chunk stress failure: "<<ex.what()<<'\n';
        return 1;
    }
}
