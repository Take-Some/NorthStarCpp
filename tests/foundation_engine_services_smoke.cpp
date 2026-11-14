// NorthStarCpp 2026: independently linked foundation runtime services.
#include <northstarcpp/frame_clock.hpp>
#include <northstarcpp/mesh_asset.hpp>
#include <northstarcpp/scene_animation.hpp>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <thread>
using namespace northstarcpp;
int main() {
    try {
        timing::FrameClock clock;
        if(clock.restart_seconds()!=0.0)
            throw std::runtime_error("Unstarted clock must produce zero");
        clock.start();
        std::this_thread::sleep_for(std::chrono::milliseconds(8));
        double elapsed=clock.restart_seconds();
        if(!(elapsed>0.001 && elapsed<5.0))
            throw std::runtime_error("Foundation QPC clock failed");
        auto model=parse_obj_mesh(
            "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");
        if(model.indices.size()!=3 || model.vertices.size()!=3)
            throw std::runtime_error("Model parser not linked");
        SceneAnimationClip clip;
        SceneAnimationKeyframe begin{};
        SceneAnimationKeyframe end{};
        end.time=2.f;
        end.pose.translation={10.f,0.f,0.f};
        clip.set_keyframes({begin,end});
        if(std::abs(clip.sample(1.f).translation.x-5.f)>0.0001f)
            throw std::runtime_error("Animation interpolation not linked");
        std::puts("Foundation clock, first-class model and animation PASS");
        return 0;
    } catch(std::exception const& e) {
        std::fprintf(stderr,"Foundation services: %s\n",e.what());
        return 1;
    }
}
