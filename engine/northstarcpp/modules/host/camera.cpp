// NorthStarCpp — independent native camera director.
#include <northstarcpp/camera.hpp>
#include <northstarcpp/camera_track.hpp>
#include <northstarcpp/physics.hpp>
#include <northstarcpp/assets.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <sstream>
#include <northstarcpp/collision_queries.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace northstarcpp {
namespace {
constexpr float pi = 3.14159265358979323846f;
bool finite(float v) { return std::isfinite(v); }
bool finite(Vector3 const& v) { return finite(v.x) && finite(v.y) && finite(v.z); }
float wrap(float angle) { return std::remainder(angle, 2.f * pi); }
float angular_lerp(float from, float to, float amount) {
    return wrap(from + std::remainder(to - from, 2.f * pi) * amount);
}
float lerp(float from, float to, float t) { return from + (to - from) * t; }
Vector3 lerp(Vector3 const& a, Vector3 const& b, float t) {
    return {lerp(a.x,b.x,t),lerp(a.y,b.y,t),lerp(a.z,b.z,t)};
}
Vector3 delta(Vector3 const& a, Vector3 const& b) {
    return {a.x-b.x,a.y-b.y,a.z-b.z};
}
float distance(Vector3 const& a, Vector3 const& b) {
    auto const d=delta(a,b);return std::sqrt(d.x*d.x+d.y*d.y+d.z*d.z);
}
void ensure(bool condition, char const* message) {
    if (!condition) throw std::invalid_argument(message);
}
void check_pose(CameraPose const& p) {
    ensure(finite(p.position) && finite(p.yaw) && finite(p.pitch) && finite(p.roll) &&
           finite(p.fov_y_degrees) && finite(p.near_plane) && finite(p.far_plane),
           "Non-finite camera pose");
    ensure(std::abs(p.pitch) < pi*.5f && p.fov_y_degrees>=25.f &&
           p.fov_y_degrees<=120.f && p.near_plane>=0.01f &&
           p.near_plane<=2.f && p.far_plane>p.near_plane+1.f &&
           p.far_plane<=10000.f, "Invalid camera projection");
}
void check_settings(CameraSettings const& s) {
    for (float v : {s.third_person_distance,s.third_person_height,s.shoulder_offset,
            s.vehicle_distance,s.vehicle_height,s.vehicle_lookahead_seconds,
            s.vehicle_distance_per_speed,s.vehicle_speed_fov,
            s.vehicle_reverse_distance,s.vehicle_roll_degrees,
            s.orbit_distance,s.orbit_speed,
            s.follow_smoothing,s.collision_radius,s.collision_padding,
            s.teleport_cut_distance,s.default_fov_y,s.aimed_fov_y,
            s.near_plane,s.far_plane})
        ensure(finite(v),"Non-finite camera settings");
    ensure(s.third_person_distance>=0.5f && s.third_person_distance<=100.f &&
           s.third_person_height>=-10.f && s.third_person_height<=20.f &&
           std::abs(s.shoulder_offset)<=10.f &&
           s.vehicle_distance>=0.5f && s.vehicle_distance<=100.f &&
           std::abs(s.vehicle_height)<=30.f &&
           s.vehicle_lookahead_seconds>=0.f && s.vehicle_lookahead_seconds<=2.f &&
           s.vehicle_distance_per_speed>=0.f && s.vehicle_distance_per_speed<=1.f &&
           s.vehicle_speed_fov>=0.f && s.vehicle_speed_fov<=2.f &&
           s.vehicle_reverse_distance>=0.5f && s.vehicle_reverse_distance<=20.f &&
           s.vehicle_roll_degrees>=0.f && s.vehicle_roll_degrees<=45.f &&
           s.orbit_distance>=0.5f && s.orbit_distance<=100.f &&
           std::abs(s.orbit_speed)<=10.f &&
           s.follow_smoothing>=0.f && s.follow_smoothing<=10.f &&
           s.collision_radius>=0.f && s.collision_radius<=2.f &&
           s.collision_padding>=0.f && s.collision_padding<=1.f &&
           s.teleport_cut_distance>=1.f && s.teleport_cut_distance<=10000.f &&
           s.default_fov_y>=25.f && s.default_fov_y<=120.f &&
           s.aimed_fov_y>=25.f && s.aimed_fov_y<=120.f &&
           s.near_plane>=0.01f && s.near_plane<=2.f &&
           s.far_plane>s.near_plane+1.f && s.far_plane<=10000.f,
           "Camera settings outside supported range");
}
CameraPose blend(CameraPose const& a, CameraPose const& b, float t) {
    CameraPose out=b;
    out.position=lerp(a.position,b.position,t);
    out.yaw=angular_lerp(a.yaw,b.yaw,t);
    out.pitch=lerp(a.pitch,b.pitch,t);
    out.roll=angular_lerp(a.roll,b.roll,t);
    out.fov_y_degrees=lerp(a.fov_y_degrees,b.fov_y_degrees,t);
    out.near_plane=lerp(a.near_plane,b.near_plane,t);
    out.far_plane=lerp(a.far_plane,b.far_plane,t);
    return out;
}
void look_at(CameraPose& p, Vector3 const& goal) {
    auto const d=delta(goal,p.position);
    p.yaw=std::atan2(d.x,d.z);
    p.pitch=std::atan2(d.y,std::hypot(d.x,d.z));
}
} // namespace

CameraSettings parse_camera_settings(std::string const& json) {
    ensure(json.size()<=16384,"Camera settings exceed 16 KiB");
    boost::property_tree::ptree data;
    std::istringstream stream(json);
    boost::property_tree::read_json(stream,data);
    ensure(data.get<int>("schema")==1,"Unsupported camera tuning schema");
    CameraSettings settings;
    settings.third_person_distance=data.get<float>("third_person.distance",settings.third_person_distance);
    settings.third_person_height=data.get<float>("third_person.height",settings.third_person_height);
    settings.shoulder_offset=data.get<float>("third_person.shoulder_offset",settings.shoulder_offset);
    settings.vehicle_distance=data.get<float>("vehicle.distance",settings.vehicle_distance);
    settings.vehicle_height=data.get<float>("vehicle.height",settings.vehicle_height);
    settings.vehicle_lookahead_seconds=data.get<float>("vehicle.lookahead_seconds",
                                                       settings.vehicle_lookahead_seconds);
    settings.vehicle_distance_per_speed=data.get<float>("vehicle.distance_per_speed",
                                                        settings.vehicle_distance_per_speed);
    settings.vehicle_speed_fov=data.get<float>("vehicle.speed_fov",
                                               settings.vehicle_speed_fov);
    settings.vehicle_reverse_distance=data.get<float>("vehicle.reverse_distance",
                                                       settings.vehicle_reverse_distance);
    settings.vehicle_roll_degrees=data.get<float>("vehicle.roll_degrees",
                                                  settings.vehicle_roll_degrees);
    settings.orbit_distance=data.get<float>("orbit.distance",settings.orbit_distance);
    settings.orbit_speed=data.get<float>("orbit.speed",settings.orbit_speed);
    settings.follow_smoothing=data.get<float>("smoothing.seconds",settings.follow_smoothing);
    settings.collision_radius=data.get<float>("collision.radius",settings.collision_radius);
    settings.collision_padding=data.get<float>("collision.padding",settings.collision_padding);
    settings.teleport_cut_distance=data.get<float>("smoothing.teleport_cut_distance",
                                                    settings.teleport_cut_distance);
    settings.default_fov_y=data.get<float>("projection.fov_y_degrees",settings.default_fov_y);
    settings.aimed_fov_y=data.get<float>("projection.aimed_fov_y_degrees",settings.aimed_fov_y);
    settings.near_plane=data.get<float>("projection.near",settings.near_plane);
    settings.far_plane=data.get<float>("projection.far",settings.far_plane);
    check_settings(settings);
    return settings;
}
CameraSettings load_camera_settings(AssetStore const& assets, std::string const& virtual_path) {
    auto const data=assets.read(virtual_path);
    return parse_camera_settings(std::string(data.begin(),data.end()));
}

CameraDirector::CameraDirector(CameraSettings settings): settings_(settings) {
    check_settings(settings_);
}
CameraDirector::~CameraDirector()=default;
CameraDirector::CameraDirector(CameraDirector&&) noexcept=default;
CameraDirector& CameraDirector::operator=(CameraDirector&&) noexcept=default;
void CameraDirector::configure(CameraSettings settings) {
    check_settings(settings);
    settings_=settings;
}
void CameraDirector::set_mode(CameraMode mode, float duration) {
    ensure(finite(duration) && duration>=0.f && duration<=10.f,
           "Invalid camera transition duration");
    if (mode_==mode) return;
    mode_=mode;
    transition_from_=pose_;
    transition_seconds_=(initialized_ ? duration : 0.f);
    transition_elapsed_=0.f;
}
void CameraDirector::set_zoom(float amount) {
    ensure(finite(amount) && amount>=0.f && amount<=1.f,"Invalid camera zoom");
    zoom_=amount;
}
void CameraDirector::set_free_pose(CameraPose pose) {
    check_pose(pose);
    free_pose_=pose;
}
void CameraDirector::set_cinematic_pose(CameraPose pose) {
    check_pose(pose);
    cinematic_pose_=pose;
}
void CameraDirector::set_track(CameraTrack track, bool loop) {
    ensure(!track.empty(), "Cannot assign an empty cinematic camera track");
    track_=std::make_unique<CameraTrack>(std::move(track));
    track_time_=0.f;
    track_loop_=loop;
    track_playing_=true;
}
void CameraDirector::clear_track() noexcept {
    track_.reset();
    track_time_=0.f;
}
void CameraDirector::seek_track(float seconds) {
    ensure(track_ && finite(seconds) && seconds>=0.f &&
           seconds<=track_->duration(), "Invalid camera track seek time");
    track_time_=seconds;
    initialized_=false; // seeking a timeline is an exact cut, not a lagged follow
}
void CameraDirector::set_track_playback_rate(float rate) {
    ensure(finite(rate)&&rate>=0.05f&&rate<=8.f,
           "Invalid cinematic playback rate");
    track_rate_=rate;
}
void CameraDirector::shake(float amplitude, float duration) {
    ensure(finite(amplitude) && finite(duration) && amplitude>=0.f &&
           amplitude<=2.f && duration>=0.f && duration<=10.f,
           "Invalid camera shake impulse");
    shake_amplitude_=amplitude;
    shake_duration_=duration;
    shake_remaining_=duration;
}
void CameraDirector::reset() {
    initialized_=false;
    transition_seconds_=transition_elapsed_=0.f;
    shake_remaining_=shake_duration_=shake_amplitude_=0.f;
    time_=orbit_phase_=0.f;
    track_time_=0.f;
}
CameraPose CameraDirector::desired(CameraTarget const& t, float dt) {
    CameraPose p;
    p.position=t.eye;
    p.yaw=wrap(t.yaw);
    p.pitch=std::clamp(t.pitch,-1.50f,1.50f);
    if (t.attachment.enabled && mode_!=CameraMode::vehicle_chase) {
        float const w=t.attachment.weight;
        p.position=lerp(p.position,t.attachment.world_position,w);
        p.yaw=angular_lerp(p.yaw,t.attachment.yaw,w);
        p.pitch=lerp(p.pitch,t.attachment.pitch,w);
        p.roll=angular_lerp(0.f,t.attachment.roll,w);
    }
    p.near_plane=settings_.near_plane;
    p.far_plane=settings_.far_plane;
    p.fov_y_degrees=lerp(settings_.default_fov_y,settings_.aimed_fov_y,zoom_);

    if (mode_==CameraMode::free) return free_pose_;
    if (mode_==CameraMode::cinematic) {
        if (!track_) return cinematic_pose_;
        if (track_playing_) {
            track_time_+=dt*track_rate_;
            float const duration=track_->duration();
            if (track_loop_ && duration>0.f)
                track_time_=std::fmod(track_time_,duration);
            else
                track_time_=std::min(track_time_,duration);
        }
        return track_->sample(track_time_);
    }
    if (mode_==CameraMode::first_person) return p;

    bool const vehicle = mode_==CameraMode::vehicle_chase;
    float heading=vehicle ? t.heading : t.yaw;
    float const vehicle_speed=vehicle ?
        std::sqrt(t.velocity.x*t.velocity.x+
                  t.velocity.y*t.velocity.y+
                  t.velocity.z*t.velocity.z) : 0.f;
    float radius=vehicle
        ? (t.reversing ? settings_.vehicle_reverse_distance
                       : settings_.vehicle_distance +
                         std::min(6.f,vehicle_speed*settings_.vehicle_distance_per_speed))
        : mode_==CameraMode::orbit ? settings_.orbit_distance
        : settings_.third_person_distance;
    if (vehicle && t.reversing)
        heading=wrap(heading+pi);
    if (mode_==CameraMode::orbit) {
        orbit_phase_=wrap(orbit_phase_+dt*settings_.orbit_speed);
        heading+=orbit_phase_;
    }
    Vector3 anchor=vehicle ? t.eye : p.position;
    if (vehicle) {
        float const lead=settings_.vehicle_lookahead_seconds;
        anchor.x+=std::clamp(t.velocity.x*lead,-6.f,6.f);
        anchor.y+=std::clamp(t.velocity.y*lead,-3.f,3.f);
        anchor.z+=std::clamp(t.velocity.z*lead,-6.f,6.f);
        p.fov_y_degrees=std::clamp(p.fov_y_degrees +
            vehicle_speed*settings_.vehicle_speed_fov,25.f,120.f);
        p.roll=std::clamp(-t.steering,-1.f,1.f) *
            settings_.vehicle_roll_degrees*(pi/180.f);
    }
    float const sn=std::sin(heading), cs=std::cos(heading);
    p.position={anchor.x-radius*sn + settings_.shoulder_offset*cs,
                anchor.y+(mode_==CameraMode::vehicle_chase
                             ? settings_.vehicle_height
                             : settings_.third_person_height),
                anchor.z-radius*cs - settings_.shoulder_offset*sn};
    look_at(p,anchor);
    p.pitch=std::clamp(p.pitch,-1.50f,1.50f);
    return p;
}

Vector3 CameraDirector::clip_line_of_sight(Vector3 pivot, Vector3 wanted,
                                   Scene const& world, std::size_t ignore) const {
    auto const ray=delta(wanted,pivot);
    float const length=distance(pivot,wanted);
    if (length<1e-5f) return pivot;
    Vector3 const direction{ray.x/length,ray.y/length,ray.z/length};
    float nearest=length;
    for (std::size_t i=0;i<world.objects.size();++i) {
        if (i==ignore) continue;
        auto const& obj=world.objects[i];
        if (!finite(obj.position) || !finite(obj.size) || obj.size<=0.f) continue;
        float const r=obj.size*.5f+settings_.collision_radius;
        math::aabb bounds;
        bounds.min={obj.position.x-r,
                    obj.position.y-settings_.collision_radius,
                    obj.position.z-r};
        bounds.max={obj.position.x+r,
                    obj.position.y+obj.size+settings_.collision_radius,
                    obj.position.z+r};
        auto const hit=collision::ray_box_entry(bounds,pivot,direction,nearest);
        if (hit) nearest=std::min(nearest,std::max(0.f,*hit-settings_.collision_padding));
    }
    return {pivot.x+direction.x*nearest,pivot.y+direction.y*nearest,
            pivot.z+direction.z*nearest};
}

CameraPose CameraDirector::update(float dt, CameraTarget const& t, Scene const* world,
                                  PhysicsScene const* physics) {
    ensure(finite(dt) && dt>=0.f && dt<=0.25f,"Invalid camera delta time");
    ensure(finite(t.eye) && finite(t.velocity) &&
           finite(t.yaw) && finite(t.pitch) && finite(t.heading) &&
           finite(t.steering) && std::abs(t.steering)<=1.f &&
           (!t.attachment.enabled ||
             (finite(t.attachment.world_position) &&
              finite(t.attachment.yaw) && finite(t.attachment.pitch) &&
              finite(t.attachment.roll) && finite(t.attachment.weight) &&
              t.attachment.weight>=0.f && t.attachment.weight<=1.f)),
           "Non-finite camera target");
    time_+=dt;
    CameraPose wanted=desired(t,dt);
    Vector3 const pivot=(t.attachment.enabled &&
            mode_!=CameraMode::vehicle_chase)
        ? lerp(t.eye,t.attachment.world_position,t.attachment.weight)
        : t.eye;
    auto clip_physics = [&](Vector3 const& desired_position) {
        auto const ray=delta(desired_position,pivot);
        float const length=distance(desired_position,pivot);
        if(length<1.0e-5f) return pivot;
        auto const hit=physics->sweep_sphere(pivot,desired_position,
            settings_.collision_radius,t.ignore_object);
        if(!hit) return desired_position;
        float const fraction=std::clamp(hit->fraction -
            settings_.collision_padding/length,0.f,1.f);
        return Vector3{pivot.x+ray.x*fraction,
                       pivot.y+ray.y*fraction,
                       pivot.z+ray.z*fraction};
    };
    bool const has_collision = mode_!=CameraMode::first_person &&
        mode_!=CameraMode::free && mode_!=CameraMode::cinematic;
    if (has_collision && physics) {
        wanted.position=clip_physics(wanted.position);
    } else if (has_collision && world) {
        wanted.position=clip_line_of_sight(pivot,wanted.position,*world,t.ignore_object);
    }
    check_pose(wanted);

    bool const snap=!initialized_ ||
        distance(pivot,previous_eye_)>settings_.teleport_cut_distance;
    previous_eye_=pivot;
    if (snap) {
        pose_=wanted;
        transition_seconds_=transition_elapsed_=0.f;
        initialized_=true;
    } else {
        float const tau=settings_.follow_smoothing;
        float const a=tau>0.f ? 1.f-std::exp(-dt/tau) : 1.f;
        // FPS camera must exactly track gameplay orientation and eye point:
        // aiming and hitscan use the same authoritative yaw/pitch.
        if (mode_==CameraMode::first_person) {
            float const previous_fov=pose_.fov_y_degrees;
            pose_=wanted;
            // FPS aiming remains exact; only the lens FOV eases.
            pose_.fov_y_degrees=lerp(previous_fov,wanted.fov_y_degrees,a);
        } else {
            pose_=blend(pose_,wanted,a);
        }
        if (transition_seconds_>0.f) {
            transition_elapsed_+=dt;
            float const progress=std::min(1.f,transition_elapsed_/transition_seconds_);
            float const eased=progress*progress*(3.f-2.f*progress);
            pose_=blend(transition_from_,pose_,eased);
            if (progress>=1.f) transition_seconds_=0.f;
        }
    }
    // Never leave a third-person camera behind an occluder after smoothing.
    if (has_collision && physics)
        pose_.position=clip_physics(pose_.position);
    else if (has_collision && world)
        pose_.position=clip_line_of_sight(pivot,pose_.position,*world,t.ignore_object);

    CameraPose render=pose_;
    if (shake_remaining_>0.f && shake_duration_>0.f) {
        float const falloff=shake_remaining_/shake_duration_;
        float const amplitude=shake_amplitude_*falloff*falloff;
        render.position.x+=std::sin(time_*41.3f)*amplitude;
        render.position.y+=std::sin(time_*35.7f+1.1f)*amplitude*.6f;
        shake_remaining_=std::max(0.f,shake_remaining_-dt);
    }
    check_pose(render);
    return render;
}

} // namespace northstarcpp
