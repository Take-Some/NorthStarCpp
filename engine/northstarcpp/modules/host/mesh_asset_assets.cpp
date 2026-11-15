// NorthStarCpp - OBJ resource through the active VFS
// Copyright (C) 2026 Kayla's Systems - Kayla Verner.
#include <northstarcpp/mesh_asset.hpp>
#include <northstarcpp/native_model.hpp>
#include <northstarcpp/assets.hpp>
#include <northstarcpp/shared_assets.hpp>
#include <limits>
#include <cmath>
#include <algorithm>
#include <stdexcept>

namespace northstarcpp {
MeshAsset load_obj_mesh(AssetStore const& store, std::string const& path) {
    // VFS accepts project Content Browser paths (e.g. Meshes/mesh.obj)
    // as well as legacy assets/models/mesh.obj. Never require a physical
    // folder convention, but reject traversal and non-OBJ resources.
    if(path.size()<5 || path.size()>256 ||
       path.compare(path.size()-4,4,".obj")!=0 ||
       path.front()=='/' || path.find(':')!=std::string::npos ||
       path.find('\\')!=std::string::npos ||
       path.find("..")!=std::string::npos ||
       path.find("//")!=std::string::npos ||
       path.find('\0')!=std::string::npos)
        throw std::invalid_argument("Unsafe or unsupported VFS OBJ reference");
    if (store.size(path)>8*1024*1024)
        throw std::invalid_argument("OBJ asset exceeds 8 MiB");
    auto bytes=store.read(path);
    return parse_obj_mesh(std::string(bytes.begin(),bytes.end()));
}
MeshAsset load_scene_mesh(AssetStore const& store, std::string const& reference) {
    auto const selector=reference.find('@');
    auto const path=reference.substr(0,selector);
    if (path.size()<5 || path.size()>256 ||
        path.front()=='/' || path.find("..")!=std::string::npos ||
        path.find(':')!=std::string::npos ||
        path.find('\\')!=std::string::npos ||
        path.find("//")!=std::string::npos)
        throw std::invalid_argument("Invalid scene mesh virtual path");
    auto const dot=path.find_last_of('.');
    auto const ext=dot==std::string::npos ? "" : path.substr(dot);
    if (ext==".obj") {
        if(selector!=std::string::npos)
            throw std::invalid_argument("OBJ does not support drawable selectors");
        return load_obj_mesh(store,path);
    }
    if (ext!=".ydd" && ext!=".ydr")
        throw std::invalid_argument("Scene model must be OBJ, YDD, or YDR");
    auto const drawables=shared_assets::load_drawables(store,path);
    if(drawables.empty())
        throw std::invalid_argument("YDD/YDR contains no drawables");
    auto selected=drawables.begin();
    if(selector!=std::string::npos) {
        auto const name=reference.substr(selector+1);
        selected=std::find_if(drawables.begin(),drawables.end(),
            [&](auto const& entry){return entry.name==name;});
        if(selected==drawables.end())
            throw std::invalid_argument("Unknown YDD/YDR drawable selector");
    }
    MeshAsset result;
    for(auto const& mesh:selected->meshes) {
        auto const first_index=result.indices.size();
        if(result.vertices.size()+mesh.vertices.size()>1000000 ||
           result.indices.size()+mesh.indices.size()>1500000)
            throw std::length_error("Scene native model exceeds render budget");
        auto const base=static_cast<std::uint32_t>(result.vertices.size());
        for(auto const& vertex:mesh.vertices)
            result.vertices.push_back({{vertex.position[0],vertex.position[1],
                vertex.position[2]},
                {vertex.normal[0],vertex.normal[1],vertex.normal[2]},
                vertex.uv[0],vertex.uv[1]});
        for(auto const index:mesh.indices) {
            if(index>=mesh.vertices.size())
                throw std::invalid_argument("Invalid native drawable mesh index");
            result.indices.push_back(base+index);
        }
        if(!mesh.indices.empty())
            result.sections.push_back({mesh.name,mesh.material,first_index,
                result.indices.size()-first_index,mesh.material_required});
    }
    if(result.vertices.empty() || result.indices.empty())
        throw std::invalid_argument("Native model has no renderable triangles");
    result.minimum=result.maximum=result.vertices.front().position;
    for(auto const& v:result.vertices) {
        result.minimum.x=std::min(result.minimum.x,v.position.x);
        result.minimum.y=std::min(result.minimum.y,v.position.y);
        result.minimum.z=std::min(result.minimum.z,v.position.z);
        result.maximum.x=std::max(result.maximum.x,v.position.x);
        result.maximum.y=std::max(result.maximum.y,v.position.y);
        result.maximum.z=std::max(result.maximum.z,v.position.z);
    }
    return result;
}
NativeModelResource load_native_model_resource(
    AssetStore const& assets, std::string const& reference)
{
    auto const separator=reference.find('@');
    auto const path=reference.substr(0,separator);
    if(path.size()<5 || path.size()>256 ||
       (path.compare(path.size()-4,4,".ydd")!=0 &&
        path.compare(path.size()-4,4,".ydr")!=0))
        throw std::invalid_argument("Native Model bundle requires YDD/YDR VFS path");
    NativeModelResource result;
    result.mesh=load_scene_mesh(assets,reference);
    if(result.mesh.sections.empty())
        throw std::invalid_argument("Native Model has no drawable sections");
    if(result.mesh.sections.size()>128)
        throw std::length_error("Native Model section budget exceeded");
    for(auto const& section:result.mesh.sections) {
        if(section.material_reference.empty()) {
            if(section.material_required)
                throw std::invalid_argument("Native Model section lacks required YMAT");
            continue;
        }
        if(result.materials.count(section.material_reference)) continue;
        try {
            auto material=load_material_definition(assets,section.material_reference);
            result.materials.emplace(section.material_reference,std::move(material));
        } catch(std::exception const&) {
            if(section.material_required)throw;
            // Optional material slots can use the documented tinted
            // fallback, without inventing an alternative texture.
        }
    }
    // YDD/YDR references use a companion .skeleton.ymt if one exists.
    // An ordinary static model must not fail because it has no skeleton.
    auto const dot=path.find_last_of('.');
    auto const skeleton_path=path.substr(0,dot)+".skeleton.ymt";
    bool has_skeleton=false;
    try {
        has_skeleton=assets.size(skeleton_path)>0;
    } catch(std::runtime_error const&) {
        // No companion rig is an ordinary static drawable.
    }
    if(has_skeleton) {
        result.skeleton=load_skeleton(assets,skeleton_path);
        result.skeleton_reference=skeleton_path;
        // Reuse the already vetted native character YDD v4 parser. Do not
        // derive weights from UV/color data or silently assign joint zero.
        // YDR v5 and other non-v4 layouts require an explicit skin decoder.
        if(path.compare(path.size()-4,4,".ydd")==0) {
            auto source=load_character_model(assets,path,*result.skeleton);
            if(source.parts.size()!=result.mesh.sections.size())
                throw std::invalid_argument("Native skin sections mismatch drawable");
            result.mesh_to_skeleton=source.mesh_to_skeleton;
            result.vertex_skin.reserve(result.mesh.vertices.size());
            std::size_t vertex_offset=0;
            for(std::size_t part=0;part<source.parts.size();++part) {
                auto const& original=source.parts[part];
                auto const& section=result.mesh.sections[part];
                if(original.mesh.name!=section.name ||
                   original.mesh.material!=section.material_reference ||
                   original.mesh.vertices.size()!=original.skin.size() ||
                   original.mesh.indices.size()!=section.index_count ||
                   vertex_offset+original.skin.size()>result.mesh.vertices.size())
                    throw std::invalid_argument("Native YDD skin / drawable topology mismatch");
                for(std::size_t i=0;i<original.skin.size();++i) {
                    auto const& input=original.mesh.vertices[i];
                    auto const& output=result.mesh.vertices[vertex_offset+i];
                    for(unsigned axis=0;axis<3;++axis)
                        if(std::abs(input.position[axis]-(&output.position.x)[axis])>1e-4f)
                            throw std::invalid_argument("Native YDD skin vertex order mismatch");
                    result.vertex_skin.push_back(original.skin[i]);
                }
                vertex_offset+=original.skin.size();
            }
            if(vertex_offset!=result.mesh.vertices.size())
                throw std::invalid_argument("Native YDD skin vertex count mismatch");
        }
    }
    return result;
}
MeshAsset skin_native_mesh_cpu(
    MeshAsset const& mesh, Skeleton const& skeleton,
    std::vector<SkinWeights> const& weights,
    std::vector<math::float4x4> const& palette)
{
    if(weights.size()!=mesh.vertices.size() ||
       palette.size()!=skeleton.joints.size() ||
       palette.empty() || palette.size()>4096)
        throw std::invalid_argument("Native skinning palette or model mismatch");
    MeshAsset output=mesh;
    for(std::size_t i=0;i<output.vertices.size();++i) {
        auto const& influences=weights[i];
        auto const& input=mesh.vertices[i];
        Vector3 position{0.f,0.f,0.f};
        Vector3 normal{0.f,0.f,0.f};
        float sum=0.f;
        for(unsigned j=0;j<8;++j) {
            auto const w=influences.weights[j];
            if(!std::isfinite(w) || w<0.f ||
               (w>0.f && influences.joints[j]>=palette.size()))
                throw std::invalid_argument("Invalid native skin influence");
            if(w==0.f)continue;
            auto const& matrix=palette[influences.joints[j]];
            auto const moved=matrix.transform_position(input.position);
            auto const direction=matrix.transform_direction(input.normal);
            position.x+=moved.x*w;position.y+=moved.y*w;position.z+=moved.z*w;
            normal.x+=direction.x*w;normal.y+=direction.y*w;normal.z+=direction.z*w;
            sum+=w;
        }
        if(std::abs(sum-1.f)>0.01f)
            throw std::invalid_argument("Native skin weights must sum to 1");
        if(!std::isfinite(position.x)||!std::isfinite(position.y)||
           !std::isfinite(position.z)||!std::isfinite(normal.x)||
           !std::isfinite(normal.y)||!std::isfinite(normal.z))
            throw std::invalid_argument("Native skin produced nonfinite geometry");
        auto const len=std::sqrt(normal.x*normal.x+normal.y*normal.y+
                                 normal.z*normal.z);
        output.vertices[i].position=position;
        if(len>1e-6f)
            output.vertices[i].normal={normal.x/len,normal.y/len,normal.z/len};
    }
    output.minimum=output.maximum=output.vertices.front().position;
    for(auto const& vertex:output.vertices) {
        output.minimum.x=std::min(output.minimum.x,vertex.position.x);
        output.minimum.y=std::min(output.minimum.y,vertex.position.y);
        output.minimum.z=std::min(output.minimum.z,vertex.position.z);
        output.maximum.x=std::max(output.maximum.x,vertex.position.x);
        output.maximum.y=std::max(output.maximum.y,vertex.position.y);
        output.maximum.z=std::max(output.maximum.z,vertex.position.z);
    }
    return output;
}
MeshAsset skin_native_model_cpu(
    NativeModelResource const& model,
    std::vector<math::float4x4> const& palette)
{
    if(!model.has_skin())
        throw std::invalid_argument("Native model has no decoded skin");
    return skin_native_mesh_cpu(model.mesh,*model.skeleton,
                                model.vertex_skin,palette);
}
} // namespace northstarcpp
