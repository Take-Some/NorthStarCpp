// NorthStarCpp replaceable engine.vehicle C ABI provider.
// Native multi-wheel tyre dynamics, ABS, TCS, occupant seats and contact handoff.
// The host's PhysicsScene remains authoritative for world collision geometry.
#include <northstarcpp/vehicle_dynamics_plugin.h>
#include "module_provider.hpp"
#include "vehicle_physics.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <map>
#include <new>
#include <stdexcept>
#include <vector>
namespace {
namespace sim=northstarcpp::vehicle_physics;
using northstarcpp::module_provider::guarded;
void require(bool valid,char const* reason) {
    if(!valid)throw std::invalid_argument(reason);
}
bool finite(float x) {return std::isfinite(x);}
constexpr float bounds=1000000.f;
using Vehicle=sim::Vehicle;
struct World {std::map<std::uint32_t,Vehicle> vehicles;};
World& checked(void* world) {
    require(world!=nullptr,"Missing engine.vehicle world");
    return *static_cast<World*>(world);
}
Vehicle& get(World& world,std::uint32_t id) {
    auto it=world.vehicles.find(id);
    if(it==world.vehicles.end())throw std::out_of_range("Unknown engine.vehicle ID");
    return it->second;
}
void* create_world() noexcept {
    void* world=nullptr;
    guarded([&]{world=new World;});
    return world;
}
void destroy_world(void* world) noexcept {delete static_cast<World*>(world);}
int attach(void* world,NorthStarVehicleDefinition const* def) noexcept {
    return guarded([&] {
        require(def&&def->size>=sizeof(*def),"Invalid vehicle definition");
        require(def->entity_index<16384 && finite(def->heading) &&
            std::abs(def->heading)<bounds &&
            std::all_of(def->position,def->position+3,
                [](float x){return finite(x)&&std::abs(x)<bounds;}) &&
            finite(def->wheelbase)&&def->wheelbase>=.5f&&def->wheelbase<=30.f &&
            finite(def->maximum_speed)&&def->maximum_speed>0&&def->maximum_speed<=90.f &&
            finite(def->acceleration)&&def->acceleration>0&&def->acceleration<=30.f &&
            finite(def->braking)&&def->braking>0&&def->braking<=60.f &&
            finite(def->maximum_steering)&&def->maximum_steering>0&&
                def->maximum_steering<=1.3f &&
            finite(def->rolling_drag)&&def->rolling_drag>=0&&def->rolling_drag<=10.f,
            "Invalid vehicle chassis dimensions or driving limits");
        auto& collection=checked(world).vehicles;
        require(collection.size()<4096,"Vehicle world capacity reached");
        Vehicle v;
        v.definition=*def;
        v.state.size=sizeof(v.state);
        v.state.entity_index=def->entity_index;
        std::copy(def->position,def->position+3,v.state.position);
        v.state.heading=sim::angle(def->heading);
        sim::default_rig(v);
        require(collection.emplace(def->entity_index,std::move(v)).second,
            "Vehicle already attached to this entity");
    });
}
int detach(void* world,std::uint32_t id) noexcept {
    return guarded([&]{
        require(checked(world).vehicles.erase(id)==1,"Unknown vehicle entity");
    });
}
int controls(void* world,std::uint32_t id,float gas,float steering,float brake) noexcept {
    return guarded([&] {
        require(finite(gas)&&finite(steering)&&finite(brake) &&
            std::abs(gas)<=1.f&&std::abs(steering)<=1.f&&brake>=0.f&&brake<=1.f,
            "Vehicle inputs must be normalized");
        auto& v=get(checked(world),id);
        v.state.throttle=gas;v.state.steering=steering;v.state.brake=brake;
        v.state.waypoint_active=0;
    });
}
int waypoint(void* world,std::uint32_t id,float x,float z,float speed,float radius) noexcept {
    return guarded([&] {
        require(finite(x)&&finite(z)&&std::abs(x)<bounds&&std::abs(z)<bounds &&
            finite(speed)&&speed>=0.f&&speed<=90.f &&
            finite(radius)&&radius>=.2f&&radius<=100.f,
            "Invalid vehicle route target");
        auto& v=get(checked(world),id);
        require(speed<=v.definition.maximum_speed,"Vehicle target speed exceeds max");
        v.target_x=x;v.target_z=z;v.target_speed=speed;v.arrival_radius=radius;
        v.state.waypoint_active=1;
    });
}
int step(void* world,float dt) noexcept {
    return guarded([&] {
        require(finite(dt)&&dt>=0&&dt<=.1f,
            "Vehicle timestep must be within 0..0.1 seconds");
        auto& entities=checked(world).vehicles;
        if(dt==0.f)return;
        // Calculate into separate per-vehicle records; commit only if all valid.
        std::vector<std::pair<std::uint32_t,Vehicle>> next;
        next.reserve(entities.size());
        for(auto const& item:entities) {
            Vehicle proposed=item.second;
            require(sim::step(proposed,dt),"Vehicle physics left valid world bounds");
            next.emplace_back(item.first,std::move(proposed));
        }
        for(auto& item:next)entities.at(item.first)=std::move(item.second);
    });
}
int read(void* world,std::uint32_t id,NorthStarVehicleState* out) noexcept {
    return guarded([&] {
        require(out&&out->size>=sizeof(*out),"Missing vehicle state output");
        *out=get(checked(world),id).state;
    });
}
int at(void* world,std::uint32_t ordinal,NorthStarVehicleState* out) noexcept {
    return guarded([&] {
        require(out&&out->size>=sizeof(*out),"Missing enumeration output");
        auto& v=checked(world).vehicles;
        require(ordinal<v.size(),"Vehicle ordinal is invalid");
        auto found=v.begin();std::advance(found,ordinal);
        *out=found->second.state;
    });
}
std::uint32_t count(void* world) noexcept {
    return world?static_cast<std::uint32_t>(
        static_cast<World*>(world)->vehicles.size()):0u;
}
bool valid_wheel(NorthStarVehicleWheelConfig const& w) {
    return w.size>=sizeof(w) &&
        std::all_of(w.mount,w.mount+3,[](float x){
            return finite(x)&&std::abs(x)<=30.f;}) &&
        finite(w.radius)&&w.radius>=.12f&&w.radius<=2.f &&
        finite(w.rest_length)&&w.rest_length>=.02f&&w.rest_length<=3.f &&
        finite(w.travel)&&w.travel>=.02f&&w.travel<=2.f &&
        finite(w.spring_rate)&&w.spring_rate>=500.f&&w.spring_rate<=500000.f &&
        finite(w.damper_rate)&&w.damper_rate>=0.f&&w.damper_rate<=100000.f &&
        finite(w.grip_long)&&w.grip_long>=.05f&&w.grip_long<=4.f &&
        finite(w.grip_lat)&&w.grip_lat>=.05f&&w.grip_lat<=4.f &&
        finite(w.steer_factor)&&w.steer_factor>=0.f&&w.steer_factor<=1.f &&
        finite(w.drive_factor)&&w.drive_factor>=0.f&&w.drive_factor<=1.f &&
        finite(w.brake_factor)&&w.brake_factor>=0.f&&w.brake_factor<=1.f &&
        finite(w.rotational_inertia)&&w.rotational_inertia>=.01f&&
        w.rotational_inertia<=2000.f;
}
int configure(void* world,std::uint32_t id,NorthStarVehicleRig const* config) noexcept {
    return guarded([&] {
        require(config&&config->size>=sizeof(*config),"Missing vehicle rig");
        auto const& c=*config;
        require(c.wheel_count>=2&&c.wheel_count<=NORTHSTARCPP_VEHICLE_MAX_WHEELS &&
            c.seat_count<=NORTHSTARCPP_VEHICLE_MAX_SEATS &&
            c.wheels&&(c.seat_count==0||c.seats) &&
            finite(c.mass)&&c.mass>=100.f&&c.mass<=100000.f &&
            finite(c.yaw_inertia)&&c.yaw_inertia>=100.f&&c.yaw_inertia<=20000000.f &&
            finite(c.center_of_mass_y)&&c.center_of_mass_y>=0.f&&
                c.center_of_mass_y<=10.f &&
            finite(c.engine_force)&&c.engine_force>0.f&&c.engine_force<=1000000.f &&
            finite(c.brake_force)&&c.brake_force>0.f&&c.brake_force<=1500000.f &&
            finite(c.aerodynamic_drag)&&c.aerodynamic_drag>=0.f&&
                c.aerodynamic_drag<=500.f &&
            finite(c.rolling_resistance)&&c.rolling_resistance>=0.f&&
                c.rolling_resistance<=.5f &&
            c.abs_enabled<=1&&c.traction_control_enabled<=1,
            "Invalid vehicle rig mass/drive system or array sizes");
        std::vector<sim::Wheel> wheels;
        wheels.reserve(c.wheel_count);
        float driving=0,braking=0;
        for(std::uint32_t i=0;i<c.wheel_count;++i) {
            require(valid_wheel(c.wheels[i]),"Invalid vehicle wheel specification");
            for(auto const& prior:wheels)
                require(std::hypot(prior.setup.mount[0]-c.wheels[i].mount[0],
                            prior.setup.mount[2]-c.wheels[i].mount[2])>.07f,
                        "Wheel mounts overlap");
            sim::Wheel wheel;
            wheel.setup=c.wheels[i];
            wheel.telemetry.size=sizeof(NorthStarVehicleWheelState);
            wheel.telemetry.in_contact=1;
            wheels.push_back(wheel);
            driving+=wheel.setup.drive_factor;
            braking+=wheel.setup.brake_factor;
        }
        require(driving>0.f&&braking>0.f,"Vehicle needs driven and brake wheels");
        std::vector<sim::Seat> seats;
        seats.reserve(c.seat_count);
        unsigned drivers=0;
        for(std::uint32_t i=0;i<c.seat_count;++i) {
            auto const& seat=c.seats[i];
            require(seat.size>=sizeof(seat)&&seat.role<=1 &&
                finite(seat.yaw)&&std::abs(seat.yaw)<10.f &&
                std::all_of(seat.position,seat.position+3,
                    [](float x){return finite(x)&&std::abs(x)<30.f;}),
                "Invalid seat transform or role");
            drivers+=seat.role;
            require(drivers<=1,"Only one driver seat is supported");
            seats.push_back({seat,0});
        }
        auto& v=get(checked(world),id);
        require(std::hypot(v.forward_speed,v.lateral_speed)<.1f,
            "Rig cannot be changed while vehicle is moving");
        // Only mutate after ALL checks have succeeded.
        v.wheels=std::move(wheels);
        v.seats=std::move(seats);
        v.mass=c.mass;v.yaw_inertia=c.yaw_inertia;
        v.center_of_mass_y=c.center_of_mass_y;
        v.engine_force=c.engine_force;v.brake_force=c.brake_force;
        v.aerodynamic_drag=c.aerodynamic_drag;
        v.rolling_resistance=c.rolling_resistance;
        v.abs_enabled=c.abs_enabled!=0;
        v.traction_enabled=c.traction_control_enabled!=0;
    });
}
std::uint32_t wheel_count(void* world,std::uint32_t id) noexcept {
    auto* w=static_cast<World*>(world);
    if(!w)return 0;
    auto found=w->vehicles.find(id);
    return found==w->vehicles.end()?0:static_cast<std::uint32_t>(found->second.wheels.size());
}
int wheel_state(void* world,std::uint32_t id,std::uint32_t ordinal,
    NorthStarVehicleWheelState* out) noexcept {
    return guarded([&] {
        require(out&&out->size>=sizeof(*out),"Missing wheel output");
        auto const& wheels=get(checked(world),id).wheels;
        require(ordinal<wheels.size(),"Wheel ordinal out of bounds");
        *out=wheels[ordinal].telemetry;
    });
}
std::uint32_t seat_count(void* world,std::uint32_t id) noexcept {
    auto* w=static_cast<World*>(world);
    if(!w)return 0;
    auto found=w->vehicles.find(id);
    return found==w->vehicles.end()?0:static_cast<std::uint32_t>(found->second.seats.size());
}
int seat_state(void* world,std::uint32_t id,std::uint32_t ordinal,
    NorthStarVehicleSeatState* out) noexcept {
    return guarded([&] {
        require(out&&out->size>=sizeof(*out),"Missing seat output");
        auto const& v=get(checked(world),id);
        require(ordinal<v.seats.size(),"Seat ordinal out of bounds");
        auto const& seat=v.seats[ordinal];
        out->size=sizeof(*out);
        out->index=ordinal;
        out->role=seat.setup.role;
        out->occupant=seat.occupant;
        auto const heading=v.state.heading;
        auto const& p=seat.setup.position;
        out->world_position[0]=v.state.position[0]+sim::rotate_x(p[0],p[2],heading);
        out->world_position[1]=v.state.position[1]+p[1];
        out->world_position[2]=v.state.position[2]+sim::rotate_z(p[0],p[2],heading);
        out->world_yaw=sim::angle(heading+seat.setup.yaw);
    });
}
int occupy_seat(void* world,std::uint32_t id,std::uint32_t ordinal,
    std::uint64_t occupant) noexcept {
    return guarded([&] {
        require(occupant!=0,"Seat occupant must be a nonzero stable ECS ID");
        auto& v=get(checked(world),id);
        require(ordinal<v.seats.size(),"Unknown seat ordinal");
        require(v.seats[ordinal].occupant==0,"Seat already occupied");
        for(auto const& s:v.seats)
            require(s.occupant!=occupant,"Occupant already in this vehicle");
        v.seats[ordinal].occupant=occupant;
    });
}
int release_seat(void* world,std::uint32_t id,std::uint32_t ordinal,
    std::uint64_t occupant) noexcept {
    return guarded([&] {
        auto& v=get(checked(world),id);
        require(ordinal<v.seats.size() && occupant!=0 &&
                v.seats[ordinal].occupant==occupant,
                "Cannot release seat for different or absent occupant");
        v.seats[ordinal].occupant=0;
    });
}
int ground_contact(void* world,std::uint32_t id,
    NorthStarVehicleGroundContact const* contact) noexcept {
    return guarded([&] {
        require(contact&&contact->size>=sizeof(*contact)&&contact->in_contact<=1 &&
            finite(contact->ground_y)&&std::abs(contact->ground_y)<bounds &&
            finite(contact->friction)&&contact->friction>=.05f&&
            contact->friction<=3.f,"Invalid vehicle wheel/ground contact");
        auto& v=get(checked(world),id);
        require(contact->wheel_index<v.wheels.size(),"Unknown wheel contact index");
        auto& w=v.wheels[contact->wheel_index];
        w.explicit_contact=true;
        w.ground_y=contact->ground_y;
        w.surface_mu=contact->friction;
        w.telemetry.in_contact=contact->in_contact;
    });
}
int motion_state(void* world,std::uint32_t id,NorthStarVehicleMotionState* out) noexcept {
    return guarded([&] {
        require(out&&out->size>=sizeof(*out),"Missing motion telemetry output");
        auto const& v=get(checked(world),id);
        *out={sizeof(*out),v.forward_speed,v.lateral_speed,v.yaw_rate,
            v.last_long_slip,v.last_lat_slip,v.distance_travelled,
            v.grounded_wheels};
    });
}
int resolve_collision(void* world,std::uint32_t id,
    NorthStarVehicleCollision const* event) noexcept {
    return guarded([&] {
        require(event&&event->size>=sizeof(*event)&&
            finite(event->position[0])&&finite(event->position[1])&&
            finite(event->position[2]) &&
            std::all_of(event->position,event->position+3,
                [](float x){return std::abs(x)<bounds;}) &&
            finite(event->normal_x)&&finite(event->normal_z)&&
            finite(event->restitution)&&event->restitution>=0.f&&
            event->restitution<=.5f,"Invalid collision response payload");
        float const length=std::hypot(event->normal_x,event->normal_z);
        require(length>.5f&&length<2.f,"Collision normal must be nonzero");
        auto& v=get(checked(world),id);
        float const nx=event->normal_x/length,nz=event->normal_z/length;
        float const heading=v.state.heading;
        float vx=sim::rotate_x(v.lateral_speed,v.forward_speed,heading);
        float vz=sim::rotate_z(v.lateral_speed,v.forward_speed,heading);
        float const into=vx*nx+vz*nz;
        if(into<0.f) {
            vx-=(1.f+event->restitution)*into*nx;
            vz-=(1.f+event->restitution)*into*nz;
        }
        // Impacts remove kinetic energy, not add it.
        vx*=.7f;vz*=.7f;v.yaw_rate*=.6f;
        v.lateral_speed=vx*std::cos(heading)-vz*std::sin(heading);
        v.forward_speed=vx*std::sin(heading)+vz*std::cos(heading);
        v.state.speed=v.forward_speed;
        std::copy(event->position,event->position+3,v.state.position);
        v.state.throttle=0.f;v.state.brake=1.f;
        v.state.waypoint_active=0;
    });
}
int configure_doors(void* world,std::uint32_t id,
    NorthStarVehicleDoorConfig const* doors,std::uint32_t count) noexcept {
    return guarded([&] {
        require(count<=NORTHSTARCPP_VEHICLE_MAX_DOORS &&
            (count==0||doors),"Invalid vehicle door array");
        std::vector<sim::Door> next;
        next.reserve(count);
        for(std::uint32_t n=0;n<count;++n) {
            auto const& d=doors[n];
            require(d.size>=sizeof(d) &&
                std::all_of(d.hinge,d.hinge+3,
                    [](float v){return finite(v)&&std::abs(v)<30.f;}) &&
                finite(d.open_angle)&&
                std::abs(d.open_angle)>=.10f&&std::abs(d.open_angle)<=2.7f &&
                finite(d.angular_speed)&&d.angular_speed>=.05f&&
                d.angular_speed<=5.f,"Invalid vehicle door hinge or speed");
            next.push_back({d,0.f,0.f});
        }
        auto& v=get(checked(world),id);
        require(std::hypot(v.forward_speed,v.lateral_speed)<.15f,
            "Cannot replace door rig while vehicle is moving");
        v.doors=std::move(next);
    });
}
int set_door(void* world,std::uint32_t id,
    std::uint32_t ordinal,std::uint32_t open) noexcept {
    return guarded([&] {
        require(open<=1u,"Door target must be closed or opened");
        auto& v=get(checked(world),id);
        require(ordinal<v.doors.size(),"Unknown vehicle door");
        if(open)require(std::hypot(v.forward_speed,v.lateral_speed)<.5f,
            "Cannot open a vehicle door while moving");
        v.doors[ordinal].target=open?1.f:0.f;
    });
}
int door_state(void* world,std::uint32_t id,
    std::uint32_t ordinal,NorthStarVehicleDoorState* out) noexcept {
    return guarded([&] {
        require(out&&out->size>=sizeof(*out),"Invalid vehicle door state buffer");
        auto const& v=get(checked(world),id);
        require(ordinal<v.doors.size(),"Unknown door index");
        auto const& d=v.doors[ordinal];
        *out={sizeof(*out),ordinal,d.fraction,d.target,
              d.setup.open_angle*d.fraction};
    });
}
std::uint32_t door_count(void* world,std::uint32_t id) noexcept {
    auto* w=static_cast<World*>(world);
    if(!w)return 0u;
    auto at=w->vehicles.find(id);
    if(at==w->vehicles.end())return 0u;
    return static_cast<std::uint32_t>(at->second.doors.size());
}
NorthStarVehicleCabinApi const cabin_api{
    NORTHSTARCPP_VEHICLE_CABIN_ABI,sizeof(NorthStarVehicleCabinApi),
    configure_doors,set_door,door_state,door_count
};
NorthStarModule const cabin_module{
    sizeof(NorthStarModule),NORTHSTARCPP_MODULE_ABI,
    NORTHSTARCPP_CAPABILITY_VEHICLE,"native","3.0.0",
    NORTHSTARCPP_VEHICLE_CABIN_ABI,sizeof(cabin_api),&cabin_api,
    northstarcpp::module_provider::last_error
};
NorthStarVehicleApi const api{
    create_world,destroy_world,attach,detach,controls,waypoint,step,read,at,count
};
NorthStarVehicleDynamicsApi const dynamics_api{
    NORTHSTARCPP_VEHICLE_DYNAMICS_ABI,sizeof(NorthStarVehicleDynamicsApi),
    configure,wheel_count,wheel_state,seat_count,seat_state,
    occupy_seat,release_seat,ground_contact,motion_state,resolve_collision
};
NorthStarModule const basic_module{
    sizeof(NorthStarModule),NORTHSTARCPP_MODULE_ABI,
    NORTHSTARCPP_CAPABILITY_VEHICLE,"native","2.0.0",
    NORTHSTARCPP_VEHICLE_ABI,sizeof(api),&api,
    northstarcpp::module_provider::last_error
};
NorthStarModule const dynamics_module{
    sizeof(NorthStarModule),NORTHSTARCPP_MODULE_ABI,
    NORTHSTARCPP_CAPABILITY_VEHICLE,"native","2.0.0",
    NORTHSTARCPP_VEHICLE_DYNAMICS_ABI,sizeof(dynamics_api),&dynamics_api,
    northstarcpp::module_provider::last_error
};
}
extern "C" NORTHSTARCPP_MODULE_EXPORT NorthStarModule const* northstar_module_query(
    std::uint32_t host,char const* capability,std::uint32_t version) {
    if(auto* result=northstarcpp::module_provider::match(
        basic_module,host,capability,version))return result;
    if(auto* result=northstarcpp::module_provider::match(
        dynamics_module,host,capability,version))return result;
    return northstarcpp::module_provider::match(
        cabin_module,host,capability,version);
}
