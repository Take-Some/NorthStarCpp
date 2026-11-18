#include "../engine/northstarcpp/modules/render/shared/sky_cloud_temporal.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <stdexcept>
using namespace northstarcpp::render;
static void require(bool success,char const* msg) {
    if(!success)throw std::runtime_error(msg);
}
int main() {
    try {
        SkyCloudHistoryTracker tracker;
        SkyCloudFrame frame{};
        frame.camera.width=384;frame.camera.height=261;
        frame.camera.fov_y_degrees=70.f;
        frame.camera.pitch=.25f;
        frame.camera.far_clip=250.f;
        frame.cloud_coverage=.75f;
        frame.cloud_phase=.25f;
        frame.sun_elevation=.75f;
        auto first=tracker.begin(frame);
        require(!first.history_valid,"First frame may not sample history");
        auto steady=tracker.begin(frame);
        require(steady.history_valid,"Steady sky should reuse history");
        std::array<float,2> px{};
        require(reproject_cloud_shell(steady.camera.current,
                                     steady.camera.previous,{192.f,130.f},px),
                "Overhead cloud reprojection failed");
        require(std::abs(px[0]-192.f)<.4f &&
                std::abs(px[1]-130.f)<.4f,
                "Identical sky cameras must map to same pixel");
        frame.camera.yaw=.04f;
        auto motion=tracker.begin(frame);
        require(motion.history_valid,"Small yaw must preserve sky history");
        require(reproject_cloud_shell(motion.camera.current,
                                     motion.camera.previous,{192.f,130.f},px),
                "Animated camera reprojection unavailable");
        require(std::hypot(px[0]-192.f,px[1]-130.f)>.5f,
                "Previous sky UV must respond to camera rotation");
        frame.cloud_coverage=.96f;
        auto weather=tracker.begin(frame);
        require(weather.weather_cut && !weather.history_valid,
                "Coverage jump must invalidate cloud history");
        frame.cloud_coverage=.96f;
        require(tracker.begin(frame).history_valid,
                "History should recover after weather cut");
        frame.camera.position[0]=4500.f;
        auto teleport=tracker.begin(frame);
        require(!teleport.history_valid &&
                has_reset(teleport.camera.reset,TemporalReset::Teleport),
                "Teleport must reset history");
        frame.camera.width=1280;
        auto resize=tracker.begin(frame);
        require(!resize.history_valid &&
                has_reset(resize.camera.reset,TemporalReset::Resize),
                "Resize must reset history");
        CloudTemporalSample current{};
        current.radiance={1.f,1.1f,1.3f};
        current.opacity=.78f;
        current.first_hit_km=2.7f;
        std::array<CloudTemporalSample,9> neighborhood{};
        neighborhood.fill(current);
        neighborhood[0].radiance={.8f,.9f,1.1f};
        neighborhood[8].radiance={1.2f,1.3f,1.5f};
        auto previous=current;
        previous.radiance={1.15f,1.2f,1.45f};
        auto resolved=resolve_cloud_temporal(
            neighborhood,previous,{1.f,0.f},true);
        require(resolved.weight>.75f && !resolved.disoccluded,
                "Stable clouds must temporally accumulate");
        previous.opacity=.18f;
        auto disoccluded=resolve_cloud_temporal(
            neighborhood,previous,{1.f,0.f},true);
        require(disoccluded.weight==0 && disoccluded.disoccluded,
                "Cloud edge disocclusion must reject stale history");
        previous=current;previous.first_hit_km=9.f;
        auto depth=resolve_cloud_temporal(neighborhood,previous,{0.f,0.f},true);
        require(depth.weight==0 && depth.disoccluded,
                "Cloud depth mismatch must reject history");
        previous=current;previous.radiance={100.f,100.f,100.f};
        auto clipped=resolve_cloud_temporal(neighborhood,previous,{0.f,0.f},true);
        require(!clipped.disoccluded&&clipped.radiance[0]<2.f,
                "History neighborhood clamping must eliminate bright trails");
        auto disabled=resolve_cloud_temporal(
            neighborhood,current,{0.f,0.f},false);
        require(disabled.weight==0.f && disabled.disoccluded,
                "Invalid history must return current cloud color");
        std::puts("PASS cloud temporal reference: reprojection, camera cuts, "
                  "weather cuts, depth/opacity disocclusion, variance clamp");
        return 0;
    }catch(std::exception const& e) {
        std::fprintf(stderr,"FAIL cloud temporal: %s\n",e.what());
        return 1;
    }
}
