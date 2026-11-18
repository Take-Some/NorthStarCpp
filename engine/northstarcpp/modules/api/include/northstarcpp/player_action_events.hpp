// NorthStarCpp engine.player action events.
// Provider-independent transitions, emitted only after real physics movement.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>
#include <string_view>

namespace northstarcpp::player_action_events {
struct Motion {
    std::uint64_t pawn=0;
    float horizontal_speed=0.f; // m/s, from physics poses
    float vertical_speed=0.f;   // m/s, positive = ascending
    float fall_distance=0.f;    // metres since highest airborne position
    float impact_speed=0.f;     // downward m/s at landing
    bool grounded=true;
    bool moving=false;
    bool landed=false;          // one physics tick, never a latched event
    char const* state="idle";   // idle, moving, jumping, falling
};
struct Event {
    char const* name;           // player.jump, player.fall, player.land, ...
    Motion motion;
    char const* previous_state;
};
// Never use raw key transitions as physics events: only actual Pawn poses
// and grounded state may generate jump/fall/landing.
class Tracker {
public:
    void reset() noexcept {active_=false; motion_={}; peak_y_=0.f;last_down_speed_=0.f;}
    bool active() const noexcept {return active_;}
    Motion const& motion() const noexcept {return motion_;}
    std::vector<Event> step(std::uint64_t pawn,float x,float y,float z,
                            bool grounded,float seconds) {
        std::vector<Event> events;
        if(!pawn||!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(z)||
           !std::isfinite(seconds)||seconds<=0.f||seconds>.5f) {
            reset();return events;
        }
        if(!active_||pawn!=motion_.pawn) {
            reset();
            active_=true;
            x_=x;y_=y;z_=z;peak_y_=y;
            motion_.pawn=pawn;
            motion_.grounded=grounded;
            motion_.state=grounded?"idle":"falling";
            if(!grounded)events.push_back({"player.fall",motion_,"idle"});
            return events;
        }
        const float dx=x-x_,dy=y-y_,dz=z-z_;
        x_=x;y_=y;z_=z;
        // Teleports, streamed sector relocations and physics rewinds must not
        // manufacture impacts, falls or huge locomotion velocities.
        if(std::hypot(dx,dz)>12.f || std::abs(dy)>12.f) {
            reset();
            active_=true;x_=x;y_=y;z_=z;peak_y_=y;
            motion_.pawn=pawn;motion_.grounded=grounded;
            motion_.state=grounded?"idle":"falling";
            return events;
        }
        auto previous=motion_;
        motion_.horizontal_speed=std::hypot(dx,dz)/seconds;
        motion_.vertical_speed=dy/seconds;
        motion_.grounded=grounded;
        motion_.landed=false;
        motion_.impact_speed=0.f;
        // Hysteresis avoids a stream of move.start/stop events at rest.
        motion_.moving=previous.moving ?
            motion_.horizontal_speed>.15f : motion_.horizontal_speed>.27f;
        if(!grounded) {
            if(previous.grounded) {
                peak_y_=std::max(y,previous.fall_distance+y);
                last_down_speed_=0.f;
            } else peak_y_=std::max(peak_y_,y);
            motion_.fall_distance=std::max(0.f,peak_y_-y);
            if(motion_.vertical_speed<0.f)
                last_down_speed_=std::max(last_down_speed_,-motion_.vertical_speed);
            motion_.state=motion_.vertical_speed>.20f?"jumping":"falling";
        } else {
            if(!previous.grounded) {
                motion_.landed=true;
                motion_.impact_speed=std::max(last_down_speed_,
                                              std::max(0.f,-motion_.vertical_speed));
                motion_.fall_distance=std::max(previous.fall_distance,
                                                std::max(0.f,peak_y_-y));
            } else motion_.fall_distance=0.f;
            motion_.state=motion_.moving?"moving":"idle";
            peak_y_=y;
            last_down_speed_=0.f;
        }
        if(previous.grounded&&!grounded) {
            events.push_back({motion_.vertical_speed>.20f?
                "player.jump":"player.fall",motion_,previous.state});
        } else if(!previous.grounded&&!grounded &&
                  std::string_view(previous.state)!=motion_.state && motion_.state=="falling") {
            events.push_back({"player.apex",motion_,previous.state});
            events.push_back({"player.fall",motion_,previous.state});
        } else if(!previous.grounded&&grounded)
            events.push_back({"player.land",motion_,previous.state});
        if(previous.moving!=motion_.moving)
            events.push_back({motion_.moving?"player.move.start":
                                            "player.move.stop",motion_,previous.state});
        if(std::string_view(previous.state)!=motion_.state)
            events.push_back({"player.state.changed",motion_,previous.state});
        return events;
    }
private:
    bool active_=false;
    float x_=0.f,y_=0.f,z_=0.f,peak_y_=0.f,last_down_speed_=0.f;
    Motion motion_{};
};
} // namespace northstarcpp::player_action_events
