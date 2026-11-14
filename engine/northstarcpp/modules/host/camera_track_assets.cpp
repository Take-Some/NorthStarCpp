// Asset access for CameraTrack remains in host; interpolation lives in animation.
#include <northstarcpp/camera_track.hpp>
#include <northstarcpp/assets.hpp>
namespace northstarcpp {
CameraTrack load_camera_track(AssetStore const& assets,std::string const& path) {
    auto const bytes=assets.read(path);
    return parse_camera_track(std::string(bytes.begin(),bytes.end()));
}
} // namespace northstarcpp
