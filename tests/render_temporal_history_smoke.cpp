#include "../engine/northstarcpp/modules/render/shared/temporal_history.hpp"
#include <cassert>
#include <cmath>
#include <stdexcept>
int main() {
    using namespace northstarcpp::render;
    TemporalCamera c;
    c.width=1280;c.height=720;
    TemporalHistoryTracker history;
    auto first=history.begin(c);
    assert(!first.history_valid);
    assert(has_reset(first.reset,TemporalReset::FirstFrame));
    auto stable=history.begin(c);
    assert(stable.history_valid && stable.reset==TemporalReset::None);
    assert(stable.generation==first.generation);
    std::array<float,2> velocity{};
    assert(temporal_pixel_velocity(stable,{0,0,10},velocity));
    assert(std::abs(velocity[0])<.001f && std::abs(velocity[1])<.001f);
    auto right=c;c.position[0]=1.f;
    auto move=history.begin(c);
    assert(move.history_valid);
    assert(temporal_pixel_velocity(move,{0,0,10},velocity));
    // The camera moves right: image moves left -> previous-current > 0.
    assert(velocity[0]>10.f && std::abs(velocity[1])<.01f);
    auto teleported=c;teleported.position[0]=1000.f;
    auto cut=history.begin(teleported);
    assert(!cut.history_valid && has_reset(cut.reset,TemporalReset::Teleport));
    assert(!temporal_pixel_velocity(cut,{0,0,10},velocity));
    c=teleported;c.yaw=3.13f;
    auto rotation=history.begin(c);
    assert(has_reset(rotation.reset,TemporalReset::Rotation));
    c.yaw=-3.13f;
    auto wrap=history.begin(c);
    assert(wrap.history_valid); // -pi/+pi wrap is a small movement.
    c.width=1920;
    auto resize=history.begin(c);
    assert(!resize.history_valid && has_reset(resize.reset,TemporalReset::Resize));
    c.fov_y_degrees=80.f;
    assert(has_reset(history.begin(c).reset,TemporalReset::Projection));
    c.first_person=false;
    assert(has_reset(history.begin(c).reset,TemporalReset::CameraMode));
    history.invalidate();
    assert(has_reset(history.begin(c).reset,TemporalReset::External));
    assert(history.begin(c).history_valid);
    c.near_clip=0.f;
    bool invalid=false;
    try{(void)history.begin(c);}catch(std::invalid_argument const&){invalid=true;}
    assert(invalid);
    c.near_clip=.1f;
    assert(history.begin(c).history_valid);
    return 0;
}
