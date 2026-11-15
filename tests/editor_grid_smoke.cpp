// NorthStarCpp camera-relative editor ground grid acceptance.
// The authoring grid must follow the camera across the full scene, remain
// bounded in CPU/GPU work, and preserve 1/2/5 logarithmic LOD divisions.
#include "../engine/northstarcpp/modules/viewport/editor_grid.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>
using namespace northstarcpp;
using namespace northstarcpp::editor_grid;
static void check(bool condition,char const* msg) {
    if(!condition)throw std::runtime_error(msg);
}
int main() {
    try {
        CameraPose camera;
        camera.position={8.f,12.f,-15.f};
        camera.yaw=0.f;
        camera.pitch=-.55f;
        camera.fov_y_degrees=60.f;
        auto const near=build(camera,1280,720);
        check(near.lines.size()>40 && near.lines.size()<=354,
              "Local grid missing or exceeds bounded draw budget");
        check(near.minor_step>0.f && near.major_step==5.f*near.minor_step,
              "Invalid adaptive minor/major grid divisions");
        for(auto const& line:near.lines) {
            check(std::isfinite(line.from.x)&&std::isfinite(line.to.z),
                  "Grid produced nonfinite geometry");
            check(line.from.y==0.f && line.to.y==0.f,
                  "Ground grid not planar");
        }
        auto const existingOrigin=std::count_if(near.lines.begin(),
            near.lines.end(),[](auto const& line){return line.origin;});
        check(existingOrigin==2,"Missing world X/Z origin axes");

        // Far away from world origin, the grid must follow the view, not
        // abruptly stop at a fixed [-100,+100] map extent.
        camera.position={81920.f,90.f,-45030.f};
        auto const moved=build(camera,1280,720);
        check(moved.lines.size()>40 && moved.lines.size()<=354,
              "Remote-camera grid unavailable or unbounded");
        check(std::abs(moved.center.x)>80000.f &&
              std::abs(moved.center.z)>44000.f,
              "Editor grid did not track distant camera");
        auto const nearSpan=near.radius;
        check(moved.radius>=nearSpan,"Distant camera grid shrank unexpectedly");

        // Camera zoom-out must produce fewer coarser divisions rather than
        // shimmering infinitely thin lines at sub-pixel spacing.
        camera.position.y=2000.f;
        auto const high=build(camera,1280,720);
        check(high.minor_step>moved.minor_step,
              "Grid LOD fails to grow with camera height");
        check(high.lines.size()<=354,"Zoomed-out grid exceeds draw budget");

        // Camera looking over the horizon still produces a planar grid
        // without division by a near-horizontal ray.
        camera.pitch=.2f;
        auto const horizon=build(camera,1920,1080);
        check(horizon.lines.size()>40 && horizon.lines.size()<=354,
              "Horizon-facing editor grid vanished");
        check(std::isfinite(horizon.radius)&&horizon.radius<=30000.f,
              "Horizon grid range became unbounded");

        // Degenerate input must never feed NaNs into render providers.
        camera.position.x=std::nanf("");
        check(build(camera,1280,720).lines.empty(),
              "Invalid camera generated nonfinite grid buffers");
        check(build(CameraPose{},0,720).lines.empty(),
              "Zero viewport size generated grid");
        std::puts("Adaptive ground grid: camera follow + LOD + bounds + horizon PASS");
        return 0;
    }catch(std::exception const& error) {
        std::fprintf(stderr,"Adaptive grid failure: %s\n",error.what());
        return 1;
    }
}
