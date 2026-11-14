// Native procedural clouds and world authoring using engine.perlin ABI 1.
#include <northstarcpp/procedural_environment.hpp>
#include <northstarcpp/perlin.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

namespace northstarcpp {
namespace {
std::uint8_t byte01(float f) {
    return static_cast<std::uint8_t>(std::lround(
        std::clamp((f+1.f)*0.5f,0.f,1.f)*255.f));
}
void require(bool good, char const* msg) {
    if (!good) throw std::invalid_argument(msg);
}
}
std::vector<std::uint8_t> make_perlin_cloud_rgba(CloudNoiseSettings settings) {
    require(settings.width>=16 && settings.height>=16 &&
        settings.width<=1024 && settings.height<=1024 &&
        static_cast<std::uint64_t>(settings.width)*settings.height<=1048576,
        "Cloud noise texture must be 16..1024 per axis and <= 1M pixels");
    // Set the lattice period to eight tiles. Integer lacunarity preserves the
    // border seam through every octave; sampling the last pixel never wraps
    // past the first pixel until the GPU's repeat sampler interpolates it.
    PerlinNoise noise({settings.seed,8,8,8});
    PerlinFractal bands;
    bands.mode=PerlinMode::fbm;
    bands.octaves=4;
    bands.frequency=1.0;
    bands.lacunarity=2.0;
    bands.persistence=0.5;
    double dx=8.0/settings.width,dy=8.0/settings.height;
    auto large=noise.fill2(0,0,dx,dy,settings.width,settings.height,bands);
    bands.octaves=2;
    auto detail=noise.fill2(2.25,3.125,dx,dy,settings.width,settings.height,bands);
    bands.octaves=3;
    auto wisps=noise.fill2(5.625,1.375,dx,dy,settings.width,settings.height,bands);
    std::vector<std::uint8_t> rgba(large.size()*4);
    for(std::size_t i=0;i<large.size();++i) {
        rgba[4*i+0]=byte01(large[i]);
        rgba[4*i+1]=byte01(detail[i]);
        rgba[4*i+2]=byte01(wisps[i]);
        rgba[4*i+3]=255;
    }
    return rgba;
}

Scene make_perlin_world(PerlinWorldSettings settings) {
    require(settings.columns>=4 && settings.columns<=64 &&
            settings.rows>=4 && settings.rows<=64 &&
            settings.max_layers>=1 && settings.max_layers<=8 &&
            settings.cell_tiles>=1 && settings.cell_tiles<=32 &&
            std::isfinite(settings.tile_size) &&
            settings.tile_size>=0.5f && settings.tile_size<=20.f &&
            static_cast<std::uint64_t>(settings.columns)*settings.rows*
            settings.max_layers+1 <= max_scene_objects,
            "Perlin world dimensions exceed scene limits");
    Scene result;
    result.version=3;
    result.name="Perlin Generated World";
    result.layers.push_back({"Terrain",true});
    result.streaming.load_radius=90.f;
    result.streaming.unload_radius=140.f;

    std::uint32_t nx=(settings.columns+settings.cell_tiles-1)/settings.cell_tiles;
    std::uint32_t nz=(settings.rows+settings.cell_tiles-1)/settings.cell_tiles;
    auto cell_name=[](std::uint32_t x,std::uint32_t z) {
        return "terrain-"+std::to_string(x)+"-"+std::to_string(z);
    };
    float tile=settings.tile_size;
    float center_x=(settings.columns-1)*0.5f;
    float center_z=(settings.rows-1)*0.5f;
    for(std::uint32_t cz=0;cz<nz;++cz)
        for(std::uint32_t cx=0;cx<nx;++cx) {
            auto x0=cx*settings.cell_tiles;
            auto z0=cz*settings.cell_tiles;
            auto x1=std::min(settings.columns,(cx+1)*settings.cell_tiles);
            auto z1=std::min(settings.rows,(cz+1)*settings.cell_tiles);
            SceneCell cell;
            cell.name=cell_name(cx,cz);
            cell.minimum={(x0-center_x-0.5f)*tile,-24.f,(z0-center_z-0.5f)*tile};
            cell.maximum={(x1-center_x-0.5f)*tile,24.f,(z1-center_z-0.5f)*tile};
            result.cells.push_back(std::move(cell));
        }

    PerlinNoise noise({settings.seed,256,256,256});
    PerlinFractal terrain;
    terrain.octaves=5;
    terrain.frequency=0.072;
    terrain.lacunarity=2;
    terrain.persistence=0.51;
    auto height=noise.fill2(0,0,1,1,settings.columns,settings.rows,terrain);
    result.objects.reserve(static_cast<std::size_t>(settings.columns)*settings.rows*
                           settings.max_layers+1);
    std::uint64_t id=1;
    float bottom=-settings.tile_size*3.f;
    std::uint32_t center_col=settings.columns/2,center_row=settings.rows/2;
    float spawn_height=0;
    for(std::uint32_t z=0;z<settings.rows;++z)
        for(std::uint32_t x=0;x<settings.columns;++x) {
            auto h=height[static_cast<std::size_t>(z)*settings.columns+x];
            auto layers=static_cast<std::uint32_t>(std::clamp(
                std::lround((h+1.f)*0.5f*(settings.max_layers-1))+1L,
                1L,static_cast<long>(settings.max_layers)));
            float top=bottom+layers*tile;
            if(x==center_col && z==center_row) spawn_height=top;
            for(std::uint32_t y=0;y<layers;++y) {
                SceneObject block;
                block.name="Terrain";
                block.node.id=id++;
                block.node.layer="Terrain";
                block.node.cell=cell_name(x/settings.cell_tiles,z/settings.cell_tiles);
                block.position={(x-center_x)*tile,bottom+(y+0.5f)*tile,
                                (z-center_z)*tile};
                block.size=tile;
                block.mass=0;
                block.node.collision=true;
                block.color=(y+1==layers) ? 0x66865a : 0x765943;
                result.objects.push_back(std::move(block));
            }
        }
    SceneObject player;
    player.name="Player";
    player.position={(center_col-center_x)*tile,spawn_height+3.f,
                     (center_row-center_z)*tile};
    player.size=1.f;
    player.color=0x46b9d8;
    player.player=true;
    player.mass=2.f;
    player.node.id=id;
    result.objects.push_back(std::move(player));
    validate_scene(result);
    return result;
}
} // namespace northstarcpp
