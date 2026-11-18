#include <northstarcpp/navigation_crowd.hpp>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace northstarcpp;
using namespace northstarcpp::navigation;
void require(bool condition,char const* what) {
    if(!condition)throw std::runtime_error(what);
}
int main() {
    auto empty=steer_crowd(1,{0,0,0},{1,0,0},1.5f,.42f,{});
    require(std::abs(empty.direction.x-1)<.001f &&
            empty.velocity_scale==1.f,"Crowd free movement must be exact");
    auto headOn=steer_crowd(1,{-2,0,0},{1,0,0},2.f,.42f,
        {{ {2,0,0},{-2,0,0},.42f,true,2 }});
    require(headOn.close_neighbors==1 && headOn.slowed,
        "Crowd solver did not react to oncoming pedestrian");
    auto headOnOther=steer_crowd(2,{2,0,0},{-1,0,0},2.f,.42f,
        {{ {-2,0,0},{2,0,0},.42f,true,1 }});
    require(headOnOther.slowed,"Other pedestrian ignored reciprocal encounter");
    auto parked=steer_crowd(22,{-1.5f,0,0},{1,0,0},2.f,.45f,
        {{{0,0,0},{0,0,0},.8f,false,90}});
    require(parked.slowed,"Stationary car obstacle did not affect route");
    auto movingCar=steer_crowd(12,{0,0,0},{0,0,1},1.6f,.45f,
        {{{2,0,3},{-3,0,0},1.2f,false,99}});
    require(movingCar.slowed,"Crossing vehicle did not affect route");
    auto invalid=steer_crowd(12,{0,0,0},{0,0,1},-3.f,.45f,{});
    require(invalid.velocity_scale==0.f,
        "Invalid crowd speed was not handled safely");
    std::cout<<"Crowd steering: clear path, head-on peers, stationary "
             <<"car, moving car, invalid input PASS\n";
}
