// Native 2026 adaptation of original skeleton and per-bone animation logic:
// validated parent-ordered hierarchy and the original row-vector math core.
#include <northstarcpp/skeletal_animation.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

namespace northstarcpp {
namespace {
bool finite(Vector3 v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}
bool finite_matrix(math::float4x4 const& m) {
    for(int r=0;r<4;++r)for(int c=0;c<4;++c)
        if(!std::isfinite(m.elements[r][c]))return false;
    return true;
}
void validate_pose(SceneAnimationPose const& p) {
    if(!finite(p.translation) || !finite(p.rotation) || !finite(p.scale) ||
       std::abs(p.translation.x)>1e6f || std::abs(p.translation.y)>1e6f ||
       std::abs(p.translation.z)>1e6f ||
       std::abs(p.rotation.x)>36000.f || std::abs(p.rotation.y)>36000.f ||
       std::abs(p.rotation.z)>36000.f ||
       p.scale.x<=0.f || p.scale.y<=0.f || p.scale.z<=0.f ||
       p.scale.x>1000.f || p.scale.y>1000.f || p.scale.z>1000.f)
        throw std::invalid_argument("Invalid skeleton bone transform");
}
SceneAnimationPose merge(SceneAnimationPose const& base, SceneAnimationPose const& offset) {
    return {
        {base.translation.x+offset.translation.x,base.translation.y+offset.translation.y,
         base.translation.z+offset.translation.z},
        {base.rotation.x+offset.rotation.x,base.rotation.y+offset.rotation.y,
         base.rotation.z+offset.rotation.z},
        {base.scale.x*offset.scale.x,base.scale.y*offset.scale.y,
         base.scale.z*offset.scale.z}
    };
}
math::float4x4 matrix(SceneAnimationPose const& p) {
    validate_pose(p);
    constexpr float radians=math::pi/180.f;
    auto const rotation=math::create_rotation(Vector3{
        p.rotation.x*radians,p.rotation.y*radians,p.rotation.z*radians});
    return math::create_scale(p.scale) * rotation *
           math::create_translation(p.translation);
}
Vector3 read_vec(boost::property_tree::ptree const& v, char const* label) {
    float items[3]{};
    int n=0;
    for(auto const& child:v) {
        if(!child.first.empty() || n>=3)
            throw std::invalid_argument(std::string("Invalid skeleton vector: ")+label);
        items[n++]=child.second.get_value<float>();
    }
    if(n!=3)throw std::invalid_argument(std::string("Invalid vector length: ")+label);
    return {items[0],items[1],items[2]};
}
Vector3 normalize(Vector3 v) {
    float length=std::sqrt(v.x*v.x+v.y*v.y+v.z*v.z);
    if(!std::isfinite(length) || length<1e-8f)return {0.f,1.f,0.f};
    return {v.x/length,v.y/length,v.z/length};
}
} // namespace

void SkeletonRig::set_bones(std::vector<SkeletonBone> bones) {
    if(bones.empty() || bones.size()>256)
        throw std::invalid_argument("Skeleton requires 1..256 bones");
    std::unordered_set<std::string> names;
    std::vector<math::float4x4> globals, inverses;
    globals.reserve(bones.size()); inverses.reserve(bones.size());
    for(std::size_t i=0;i<bones.size();++i) {
        auto const& bone=bones[i];
        if(bone.name.empty() || bone.name.size()>128 ||
           !names.insert(bone.name).second ||
           (bone.parent!=-1 && (bone.parent<0 ||
            static_cast<std::size_t>(bone.parent)>=i)))
            throw std::invalid_argument("Invalid skeleton bone name or parent order");
        auto global=matrix(bone.bind);
        if(bone.parent>=0)global=global*globals[static_cast<std::size_t>(bone.parent)];
        if(!finite_matrix(global))
            throw std::invalid_argument("Nonfinite skeleton global bind");
        math::float4x4 inverse;
        if(!inverse.try_invert(global) || !finite_matrix(inverse))
            throw std::invalid_argument("Non-invertible skeleton bind pose");
        globals.push_back(global);
        inverses.push_back(inverse);
    }
    bones_=std::move(bones);
    inverse_bind_=std::move(inverses);
}

std::vector<math::float4x4> SkeletonRig::skinning_palette(
    std::vector<SceneAnimationClip> const& tracks, float seconds) const {
    if(bones_.empty() || tracks.size()>bones_.size() ||
       !std::isfinite(seconds) || seconds<0.f)
        throw std::invalid_argument("Invalid skeleton playback request");
    std::vector<math::float4x4> globals, palette;
    globals.reserve(bones_.size());palette.reserve(bones_.size());
    for(std::size_t i=0;i<bones_.size();++i) {
        auto pose=bones_[i].bind;
        if(i<tracks.size() && tracks[i].keyframe_count())
            pose=merge(pose,tracks[i].sample(seconds));
        auto global=matrix(pose);
        if(bones_[i].parent>=0)
            global=global*globals[static_cast<std::size_t>(bones_[i].parent)];
        if(!finite_matrix(global))
            throw std::invalid_argument("Invalid animated bone global");
        auto skin=inverse_bind_[i]*global;
        if(!finite_matrix(skin))
            throw std::invalid_argument("Invalid skeletal skinning matrix");
        globals.push_back(global);
        palette.push_back(skin);
    }
    return palette;
}

SkeletonRig parse_skeleton_rig(std::string const& source) {
    if(source.empty() || source.size()>1024*1024 || source.find('\0')!=std::string::npos)
        throw std::invalid_argument("Skeleton JSON exceeds 1 MiB or is invalid");
    std::istringstream stream(source);
    boost::property_tree::ptree document;
    boost::property_tree::read_json(stream,document);
    if(document.get<int>("schema")!=1 ||
       document.get<std::string>("type")!="skeleton")
        throw std::invalid_argument("Expected skeleton schema 1");
    std::vector<SkeletonBone> bones;
    for(auto const& item:document.get_child("bones")) {
        if(!item.first.empty() || bones.size()>=256)
            throw std::invalid_argument("Invalid or oversized skeleton bones array");
        auto const& value=item.second;
        SkeletonBone bone;
        bone.name=value.get<std::string>("name");
        bone.parent=value.get<std::int32_t>("parent",-1);
        if(auto v=value.get_child_optional("translation"))
            bone.bind.translation=read_vec(*v,"translation");
        if(auto v=value.get_child_optional("rotation"))
            bone.bind.rotation=read_vec(*v,"rotation");
        if(auto v=value.get_child_optional("scale"))
            bone.bind.scale=read_vec(*v,"scale");
        bones.push_back(std::move(bone));
    }
    SkeletonRig skeleton;
    skeleton.set_bones(std::move(bones));
    return skeleton;
}

std::vector<float> pack_skinning_palette(std::vector<math::float4x4> const& palette) {
    if(palette.empty() || palette.size()>256)
        throw std::invalid_argument("GPU palette requires 1..256 bones");
    std::vector<float> packed;
    packed.reserve(palette.size()*16);
    for(auto const& matrix:palette) {
        if(!finite_matrix(matrix))throw std::invalid_argument("Invalid GPU palette matrix");
        for(int row=0;row<4;++row)
            for(int col=0;col<4;++col)
                packed.push_back(matrix.elements[row][col]);
    }
    return packed;
}

MeshAsset skin_mesh_cpu(std::vector<SkinnedVertex> const& vertices,
                        std::vector<std::uint32_t> const& indices,
                        std::vector<math::float4x4> const& palette) {
    if(vertices.empty() || vertices.size()>250000 || indices.empty() ||
       indices.size()%3 || indices.size()>300000 ||
       palette.empty() || palette.size()>256)
        throw std::invalid_argument("Invalid skinned mesh buffer sizes");
    MeshAsset output;
    output.indices=indices;
    for(auto idx:indices)
        if(idx>=vertices.size())throw std::out_of_range("Skinned mesh index out of bounds");
    std::vector<math::float4x4> normal_transforms;
    normal_transforms.reserve(palette.size());
    for(auto const& skin:palette) {
        if(!finite_matrix(skin))throw std::invalid_argument("Nonfinite skinning matrix");
        math::float4x4 inverse;
        if(!inverse.try_invert(skin))
            throw std::invalid_argument("Singular skinning matrix");
        normal_transforms.push_back(math::transpose(inverse));
    }
    output.vertices.reserve(vertices.size());
    for(auto const& input:vertices) {
        if(!finite(input.vertex.position) || !finite(input.vertex.normal) ||
           !std::isfinite(input.vertex.u) || !std::isfinite(input.vertex.v))
            throw std::invalid_argument("Nonfinite skinned vertex");
        float total=0.f;
        for(int influence=0;influence<4;++influence) {
            auto const weight=input.skin.weight[influence];
            if(!std::isfinite(weight) || weight<0.f || weight>1.f ||
               (weight>0.f && input.skin.bone[influence]>=palette.size()))
                throw std::invalid_argument("Invalid vertex bone index or weight");
            total+=weight;
        }
        if(total<1e-6f || total>1.001f || total<0.999f)
            throw std::invalid_argument("Skinning vertex weights must total 1");
        Vector3 point{0.f,0.f,0.f},norm{0.f,0.f,0.f};
        for(int influence=0;influence<4;++influence) {
            float const weight=input.skin.weight[influence]/total;
            if(weight<=0.f)continue;
            auto bone=input.skin.bone[influence];
            auto pos=palette[bone].transform_position(input.vertex.position);
            auto normal=normal_transforms[bone].transform_direction(input.vertex.normal);
            point.x+=pos.x*weight;point.y+=pos.y*weight;point.z+=pos.z*weight;
            norm.x+=normal.x*weight;norm.y+=normal.y*weight;norm.z+=normal.z*weight;
        }
        if(!finite(point)||!finite(norm))
            throw std::invalid_argument("Skinning produced nonfinite vertices");
        MeshVertex result=input.vertex;
        result.position=point;result.normal=normalize(norm);
        output.vertices.push_back(result);
    }
    output.minimum=output.maximum=output.vertices.front().position;
    for(auto const& v:output.vertices) {
        output.minimum.x=std::min(output.minimum.x,v.position.x);
        output.minimum.y=std::min(output.minimum.y,v.position.y);
        output.minimum.z=std::min(output.minimum.z,v.position.z);
        output.maximum.x=std::max(output.maximum.x,v.position.x);
        output.maximum.y=std::max(output.maximum.y,v.position.y);
        output.maximum.z=std::max(output.maximum.z,v.position.z);
    }
    return output;
}
} // namespace northstarcpp
