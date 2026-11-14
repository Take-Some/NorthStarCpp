// NEF8 Scene ListFile v2: independent streaming-cell block regression.
// Copyright (C) 2026 Kayla's Systems - Kayla Verner.
#include <northstarcpp/scene.hpp>
#include <northstarcpp/scene_chunk_index.hpp>
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>

int main(int argc, char** argv) {
    using namespace northstarcpp;
    namespace fs=std::filesystem;
    if(argc!=2)return 99;
    fs::path const destination=fs::u8path(argv[1]);
    fs::path const tampered=destination.parent_path()/"scene-chunk-corrupt.nscn";
    try {
        Scene authored;
        authored.version=3;
        authored.name="Chunked streaming test";
        authored.cells.push_back({"SectorA",{-50.f,-5.f,-50.f},{0.f,50.f,0.f},false});
        authored.cells.push_back({"SectorB",{0.f,-5.f,0.f},{50.f,50.f,50.f},false});
        SceneObject global;
        global.name="Persistent Light";
        global.node.id=10;
        authored.objects.push_back(global);
        SceneObject a;
        a.name="Sector A Object";
        a.position={-10.f,0.f,-10.f};
        a.node.id=20;
        a.node.cell="SectorA";
        a.node.asset="models/sector_a.ydd";
        authored.objects.push_back(a);
        SceneObject b;
        b.name="Sector B Object";
        b.position={10.f,0.f,10.f};
        b.node.id=30;
        b.node.cell="SectorB";
        authored.objects.push_back(b);
        save_scene(authored,destination);
        auto const chunks=index_scene_chunks(destination);
        if(chunks.size()!=3 || !chunks[0].cell.empty() ||
           chunks[1].cell!="SectorA" || chunks[2].cell!="SectorB" ||
           chunks[0].record_count!=4 ||
           chunks[1].record_count!=1 || chunks[2].record_count!=1)
            return 1;
        auto const persistent=load_scene_chunk(destination,"");
        auto const sector_a=load_scene_chunk(destination,"SectorA");
        auto const sector_b=load_scene_chunk(destination,"SectorB");
        if(persistent.size()!=4 || sector_a.size()!=1 || sector_b.size()!=1 ||
           sector_a[0].kind!=2 || sector_a[0].entity_id!=20 ||
           sector_b[0].kind!=2 || sector_b[0].entity_id!=30 ||
           !load_scene_chunk(destination,"NonExistent").empty())
            return 2;
        auto restored=load_scene(destination);
        if(restored.objects.size()!=3 ||
           restored.objects[1].node.cell!="SectorA" ||
           restored.objects[2].node.cell!="SectorB")
            return 3;

        // A broken compressed block must not poison indexed access to other
        // cells. A complete scene load must still reject the corrupted file.
        std::ifstream input(destination,std::ios::binary);
        std::string bytes((std::istreambuf_iterator<char>(input)),
                          std::istreambuf_iterator<char>());
        auto const offset=chunks[1].file_offset+chunks[1].compressed_bytes/2u;
        if(offset>=bytes.size())return 4;
        bytes[static_cast<std::size_t>(offset)]^=0x55;
        std::ofstream output(tampered,std::ios::binary|std::ios::trunc);
        output.write(bytes.data(),static_cast<std::streamsize>(bytes.size()));
        output.close();
        if(load_scene_chunk(tampered,"SectorB").size()!=1)return 5;
        bool corrupted_rejected=false;
        try { (void)load_scene_chunk(tampered,"SectorA"); }
        catch(std::runtime_error const&){corrupted_rejected=true;}
        if(!corrupted_rejected)return 6;
        corrupted_rejected=false;
        try { (void)load_scene(tampered); }
        catch(std::runtime_error const&){corrupted_rejected=true;}
        if(!corrupted_rejected)return 7;
        std::error_code ec;
        fs::remove(destination,ec);
        fs::remove(tampered,ec);
        return 0;
    } catch (std::exception const&) {
        return 10;
    }
}
