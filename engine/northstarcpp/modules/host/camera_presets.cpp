// NorthStarCpp: shared camera-shot preset codec, native Scene Camera Studio.
// Asset bytes are loaded via the active engine.assets provider, never from
// an external proprietary camera subsystem.
#include <northstarcpp/camera.hpp>
#include <northstarcpp/assets.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

namespace northstarcpp {
namespace {
constexpr float rad=0.01745329251994329577f;
constexpr float deg=57.295779513082320876f;
}
std::vector<CameraShot> parse_camera_presets(std::string const& bytes) {
    if(bytes.size()>65536)
        throw std::length_error("Camera preset document exceeds 64 KiB");
    boost::property_tree::ptree data;
    std::istringstream stream(bytes);
    boost::property_tree::read_json(stream,data);
    if(data.get<int>("schema")!=1)
        throw std::invalid_argument("Unsupported camera presets schema");
    auto const& entries=data.get_child("presets");
    if(entries.size()>32)
        throw std::length_error("Camera presets exceed 32 entries");
    std::vector<CameraShot> result;
    result.reserve(entries.size());
    std::unordered_set<std::string> seen;
    for(auto const& entry:entries) {
        if(!entry.first.empty())
            throw std::invalid_argument("Camera presets must be an array");
        auto const& node=entry.second;
        CameraShot shot;
        shot.source=node.get<std::string>("source");
        shot.priority=node.get<int>("priority",0);
        shot.blend_seconds=node.get<float>("blend_seconds",.35f);
        shot.pose.position={
            node.get<float>("position.x"),
            node.get<float>("position.y"),
            node.get<float>("position.z")
        };
        shot.pose.yaw=node.get<float>("rotation.yaw_degrees")*rad;
        shot.pose.pitch=node.get<float>("rotation.pitch_degrees")*rad;
        shot.pose.roll=node.get<float>("rotation.roll_degrees",0.f)*rad;
        shot.pose.fov_y_degrees=node.get<float>("projection.fov_y_degrees",75.f);
        shot.pose.near_plane=node.get<float>("projection.near",.08f);
        shot.pose.far_plane=node.get<float>("projection.far",500.f);
        validate_camera_shot(shot);
        if(!seen.insert(shot.source).second)
            throw std::invalid_argument("Duplicate camera preset source");
        result.push_back(std::move(shot));
    }
    return result;
}
std::string serialize_camera_presets(std::vector<CameraShot> const& shots) {
    if(shots.size()>32)
        throw std::length_error("Camera presets exceed 32 entries");
    std::unordered_set<std::string> seen;
    std::ostringstream output;
    output.imbue(std::locale::classic());
    output<<std::setprecision(9);
    output<<"{\n  \"schema\": 1,\n  \"presets\": [\n";
    for(std::size_t i=0;i<shots.size();++i) {
        auto const& shot=shots[i];
        validate_camera_shot(shot);
        if(!seen.insert(shot.source).second)
            throw std::invalid_argument("Duplicate camera preset source");
        if(i)output<<",\n";
        output<<"    {\"source\":\""<<shot.source<<"\","
              <<"\"priority\":"<<shot.priority<<","
              <<"\"blend_seconds\":"<<shot.blend_seconds<<","
              <<"\"position\":{\"x\":"<<shot.pose.position.x
              <<",\"y\":"<<shot.pose.position.y
              <<",\"z\":"<<shot.pose.position.z<<"},"
              <<"\"rotation\":{\"yaw_degrees\":"<<shot.pose.yaw*deg
              <<",\"pitch_degrees\":"<<shot.pose.pitch*deg
              <<",\"roll_degrees\":"<<shot.pose.roll*deg<<"},"
              <<"\"projection\":{\"fov_y_degrees\":"<<shot.pose.fov_y_degrees
              <<",\"near\":"<<shot.pose.near_plane
              <<",\"far\":"<<shot.pose.far_plane<<"}}";
    }
    output<<"\n  ]\n}\n";
    auto result=output.str();
    if(result.size()>65536)
        throw std::length_error("Camera presets exceed 64 KiB");
    return result;
}
std::vector<CameraShot> load_camera_presets(AssetStore const& assets,
                                            std::string const& virtual_path) {
    auto data=assets.read(virtual_path);
    if(data.size()>65536)
        throw std::length_error("Camera preset file exceeds 64 KiB");
    return parse_camera_presets(std::string(data.begin(),data.end()));
}
} // namespace northstarcpp
