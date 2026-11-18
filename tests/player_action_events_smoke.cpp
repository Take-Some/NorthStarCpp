#include <northstarcpp/player_action_events.hpp>
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <string_view>
#include <vector>

using northstarcpp::player_action_events::Tracker;
using northstarcpp::player_action_events::Event;

static bool has(std::vector<Event> const& events,std::string_view name) {
    return std::any_of(events.begin(),events.end(),
        [&](auto const& value){return std::string_view(value.name)==name;});
}
int main() {
    Tracker t;
    auto first=t.step(17,0,0,0,true,.1f);
    assert(first.empty());
    assert(t.motion().grounded && t.active());
    auto moving=t.step(17,0,0,0.4f,true,.1f);
    assert(has(moving,"player.move.start"));
    assert(has(moving,"player.state.changed"));
    // Grounded -> ascending: jump is based on physics, not an input key.
    auto jump=t.step(17,0,.4f,.4f,false,.1f);
    assert(has(jump,"player.jump"));
    assert(t.motion().vertical_speed>0 && !t.motion().grounded);
    assert(!has(jump,"player.land"));
    auto rise=t.step(17,0,.9f,.4f,false,.1f);
    assert(!has(rise,"player.jump"));
    auto apex=t.step(17,0,.88f,.4f,false,.1f);
    assert(has(apex,"player.apex")&&has(apex,"player.fall"));
    auto fall=t.step(17,0,.48f,.4f,false,.1f);
    assert(!has(fall,"player.fall"));
    auto land=t.step(17,0,0.f,.4f,true,.1f);
    assert(has(land,"player.land"));
    assert(t.motion().landed && t.motion().grounded);
    assert(t.motion().impact_speed>=4.7f);
    assert(t.motion().fall_distance>.8f);
    auto stable=t.step(17,0,0,.4f,true,.1f);
    assert(stable.empty());
    assert(!t.motion().landed);
    // Walk stops after actual velocity falls beneath the hysteresis limit.
    auto stop=t.step(17,0,0,.4f,true,.1f);
    assert(!has(stop,"player.land"));
    // A different pawn is an isolated session, no inherited fall events.
    auto change=t.step(53,0,50,0,true,.1f);
    assert(change.empty() && t.motion().pawn==53);
    // Teleport resets transition history, preventing artificial impacts.
    auto teleport=t.step(53,0,-200,0,false,.1f);
    assert(teleport.empty());
    auto initial_fall=t.step(53,0,-201,0,false,.1f);
    assert(!has(initial_fall,"player.land"));
    t.reset();
    assert(!t.active());
    std::puts("PASS player action: jump apex fall land movement, impact, teleport, reset");
}
