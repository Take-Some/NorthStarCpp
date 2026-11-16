// NorthStarCpp — independent native camera director.
#include <northstarcpp/camera.hpp>
#include <northstarcpp/scene_camera_actor.hpp>
#include <northstarcpp/camera_track.hpp>
#include <northstarcpp/camera_cutscene.hpp>
#include <northstarcpp/physics.hpp>
#include <northstarcpp/assets.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <sstream>
#include <northstarcpp/collision_queries.hpp>
#include <algorithm>
#include <cctype>
#include <utility>
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
// Closed-form critically damped spring: invariant for fixed targets.
void critically_damped(float& x,float& velocity,float goal,float omega,
                       float dt) noexcept {
    float const offset=x-goal;
    float const impulse=(velocity+omega*offset)*dt;
    float const decay=std::exp(-omega*dt);
    x=goal+(offset+impulse)*decay;
    velocity=(velocity-omega*impulse)*decay;
}
Vector3 bounded(Vector3 v,float max_distance) noexcept {
    auto const magnitude=std::hypot(v.x,v.y,v.z);
    if (magnitude>max_distance && magnitude>0.f) {
        auto const ratio=max_distance/magnitude;
        v.x*=ratio;v.y*=ratio;v.z*=ratio;
    }
    return v;
}
void ensure(bool condition, char const* message) {
    if (!condition) throw std::invalid_argument(message);
}
void check_pose(CameraPose const& p) {
    if(!(finite(p.position) && finite(p.yaw) && finite(p.pitch) &&
         finite(p.roll) && finite(p.fov_y_degrees) &&
         finite(p.near_plane) && finite(p.far_plane))) {
        std::ostringstream message;
        message<<"Non-finite camera pose x="<<p.position.x
               <<" y="<<p.position.y<<" z="<<p.position.z
               <<" yaw="<<p.yaw<<" pitch="<<p.pitch
               <<" roll="<<p.roll<<" fov="<<p.fov_y_degrees
               <<" near="<<p.near_plane<<" far="<<p.far_plane;
        throw std::invalid_argument(message.str());
    }
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
            s.near_plane,s.far_plane,s.follow_spring_frequency_hz,
            s.third_person_lookahead_seconds,s.third_person_lookahead_max,
            s.lookahead_damping_seconds,s.collision_restore_seconds})
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
           s.far_plane>s.near_plane+1.f && s.far_plane<=10000.f &&
           s.follow_spring_frequency_hz>=0.f &&
           s.follow_spring_frequency_hz<=20.f &&
           s.third_person_lookahead_seconds>=0.f &&
           s.third_person_lookahead_seconds<=2.f &&
           s.third_person_lookahead_max>=0.f &&
           s.third_person_lookahead_max<=20.f &&
           s.lookahead_damping_seconds>=0.f &&
           s.lookahead_damping_seconds<=2.f &&
           s.collision_restore_seconds>=0.f &&
           s.collision_restore_seconds<=4.f,
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

void validate_camera_shot(CameraShot const& shot) {
    ensure(!shot.source.empty() && shot.source.size()<=80,
           "Camera shot source must contain 1..80 characters");
    for(unsigned char c:shot.source)
        ensure(std::isalnum(c) || c=='.' || c=='_' || c=='-' || c==':',
               "Camera shot source contains unsupported characters");
    ensure(shot.priority>=-100 && shot.priority<=100,
           "Camera shot priority must be -100..100");
    ensure(finite(shot.blend_seconds) &&
           shot.blend_seconds>=0.f && shot.blend_seconds<=10.f,
           "Camera shot blend must be 0..10 seconds");
    check_pose(shot.pose);
}

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
    settings.follow_spring_frequency_hz=data.get<float>(
        "smoothing.spring_frequency_hz",settings.follow_spring_frequency_hz);
    settings.third_person_lookahead_seconds=data.get<float>(
        "third_person.lookahead_seconds",settings.third_person_lookahead_seconds);
    settings.third_person_lookahead_max=data.get<float>(
        "third_person.lookahead_max",settings.third_person_lookahead_max);
    settings.lookahead_damping_seconds=data.get<float>(
        "smoothing.lookahead_seconds",settings.lookahead_damping_seconds);
    settings.collision_restore_seconds=data.get<float>(
        "collision.restore_seconds",settings.collision_restore_seconds);
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
    if (mode_==mode && !cutscene_) return;
    // Explicit gameplay mode switches relinquish cutscene ownership.
    if(cutscene_)stop_cutscene(0.f);
    transition_source_mode_=mode_;
    mode_=mode;
    follow_velocity_={0.f,0.f,0.f};
    lookahead_offset_={0.f,0.f,0.f};
    collision_limit_=-1.f;
    transition_from_=pose_;
    transition_seconds_=(initialized_ ? duration : 0.f);
    transition_elapsed_=0.f;
}
void CameraDirector::push_shot(CameraShot shot) {
    validate_camera_shot(shot);
    auto const found=std::find_if(shots_.begin(),shots_.end(),
        [&](SequencedShot const& entry){
            return entry.shot.source==shot.source;
        });
    if(found==shots_.end() && shots_.size()>=16)
        throw std::length_error("Camera director supports at most 16 active shots");
    auto const next_order=++shot_order_;
    if(found==shots_.end())
        shots_.push_back({std::move(shot),next_order});
    else {
        found->shot=std::move(shot);
        found->order=next_order;
    }
}
void CameraDirector::clear_shot(std::string const& source) {
    shots_.erase(std::remove_if(shots_.begin(),shots_.end(),
        [&](SequencedShot const& entry){
            return entry.shot.source==source;
        }),shots_.end());
}
void CameraDirector::clear_shots() noexcept {
    shots_.clear();
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
void CameraDirector::play_cutscene(CameraCutscene scene, bool loop,
                                    float entry, float exit) {
    ensure(!scene.empty() && finite(entry) && finite(exit) &&
           entry>=0.f && entry<=10.f && exit>=0.f && exit<=10.f,
           "Invalid camera cutscene or entry/exit blend");
    transition_from_=pose_;
    transition_seconds_=initialized_?entry:0.f;
    transition_elapsed_=0.f;
    follow_velocity_={0.f,0.f,0.f};
    cutscene_=std::make_unique<CameraCutscene>(std::move(scene));
    cutscene_time_=0.f;
    cutscene_exit_seconds_=exit;
    cutscene_loop_=loop;
    cutscene_playing_=true;
    cutscene_finished_=false;
    cutscene_shot_.clear();
    cutscene_anchor_valid_=false;
    cutscene_first_tick_=true;
    pending_cutscene_cues_json_="[]";
    cutscene_exposure_ev_=0.f;
    cutscene_lens_active_=true;
}
void CameraDirector::stop_cutscene(float exit) {
    ensure(finite(exit) && exit>=0.f && exit<=10.f,
           "Invalid camera cutscene exit blend");
    if(!cutscene_)return;
    cutscene_.reset();
    cutscene_shot_.clear();
    cutscene_anchor_valid_=false;
    cutscene_first_tick_=false;
    cutscene_lens_active_=false;
    cutscene_exposure_ev_=0.f;
    pending_cutscene_cues_json_="[]";
    cutscene_time_=0.f;
    cutscene_finished_=false;
    cutscene_playing_=true;
    follow_velocity_={0.f,0.f,0.f};
    lookahead_offset_={0.f,0.f,0.f};
    collision_limit_=-1.f;
    transition_from_=pose_;
    transition_seconds_=initialized_?exit:0.f;
    transition_elapsed_=0.f;
}
void CameraDirector::seek_cutscene(float seconds) {
    ensure(cutscene_ && finite(seconds) && seconds>=0.f &&
           seconds<=cutscene_->duration(),"Invalid cutscene seek");
    cutscene_time_=seconds;
    cutscene_first_tick_=false;
    pending_cutscene_cues_json_="[]";
    cutscene_finished_=false;
    // Timeline seek is a deliberate cut; no frame-space temporal trail.
    cut();
}
std::string CameraDirector::drain_cutscene_cues_json() {
    std::string result=std::move(pending_cutscene_cues_json_);
    pending_cutscene_cues_json_="[]";
    return result.empty()?std::string("[]"):result;
}
CameraCinematicLens CameraDirector::cinematic_lens() const {
    return cutscene_?cutscene_->lens_at(cutscene_time_):CameraCinematicLens{};
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
void CameraDirector::cut() noexcept {
    initialized_=false;
    transition_seconds_=transition_elapsed_=0.f;
    follow_velocity_={0.f,0.f,0.f};
    lookahead_offset_={0.f,0.f,0.f};
    collision_limit_=-1.f;
}
void CameraDirector::reset() {
    cut();
    clear_shots();
    active_shot_.clear();
    transition_seconds_=transition_elapsed_=0.f;
    shake_remaining_=shake_duration_=shake_amplitude_=0.f;
    time_=orbit_phase_=0.f;
    track_time_=0.f;
    cutscene_.reset();
    cutscene_shot_.clear();
    cutscene_anchor_valid_=false;
    cutscene_first_tick_=false;
    cutscene_lens_active_=false;
    cutscene_exposure_ev_=0.f;
    pending_cutscene_cues_json_="[]";
    cutscene_time_=0.f;
    cutscene_finished_=false;
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
        std::hypot(t.velocity.x,t.velocity.y,t.velocity.z) : 0.f;
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
    // Independent predictive framing with a bounded, damped lead offset.
    // math::float3 has a user-declared default constructor; do not assume
    // brace-default construction clears all components on every compiler.
    Vector3 predicted{0.f,0.f,0.f};
    if (vehicle) {
        predicted={std::clamp(t.velocity.x*settings_.vehicle_lookahead_seconds,
                              -6.f,6.f),
                   std::clamp(t.velocity.y*settings_.vehicle_lookahead_seconds,
                              -3.f,3.f),
                   std::clamp(t.velocity.z*settings_.vehicle_lookahead_seconds,
                              -6.f,6.f)};
    } else if (mode_==CameraMode::third_person) {
        predicted=bounded({
            t.velocity.x*settings_.third_person_lookahead_seconds,
            t.velocity.y*settings_.third_person_lookahead_seconds,
            t.velocity.z*settings_.third_person_lookahead_seconds
        },settings_.third_person_lookahead_max);
    }
    float const lead_tau=settings_.lookahead_damping_seconds;
    float const amount=lead_tau>0.f?1.f-std::exp(-dt/lead_tau):1.f;
    if (!initialized_)lookahead_offset_=predicted;
    else lookahead_offset_=lerp(lookahead_offset_,predicted,amount);
    anchor.x+=lookahead_offset_.x;
    anchor.y+=lookahead_offset_.y;
    anchor.z+=lookahead_offset_.z;
    if (vehicle) {
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
        if (world.version==3 && (!obj.node.visible || !obj.node.collision))
            continue;
        if (!finite(obj.position) || !finite(obj.size) || obj.size<=0.f) continue;
        // Cheap conservative bound rejects distant actors before creating
        // their AABB or entering narrow-phase ray/box intersection. This is
        // valuable for large streamed cities when the physics provider is
        // temporarily unavailable during editor or startup previews.
        float const r=obj.size*.5f+settings_.collision_radius;
        float const sphere=1.7320508075688772f*r;
        Vector3 const center{obj.position.x,
            obj.position.y+obj.size*.5f,obj.position.z};
        Vector3 const relative=delta(center,pivot);
        float const along=relative.x*direction.x+
                          relative.y*direction.y+
                          relative.z*direction.z;
        if (along < -sphere || along > nearest+sphere)continue;
        float const perpendicular_squared=std::max(0.f,
            relative.x*relative.x+relative.y*relative.y+
            relative.z*relative.z-along*along);
        if(perpendicular_squared>sphere*sphere)continue;
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
    Vector3 const pivot=(t.attachment.enabled &&
            mode_!=CameraMode::vehicle_chase)
        ? lerp(t.eye,t.attachment.world_position,t.attachment.weight)
        : t.eye;
    if(initialized_ &&
       distance(pivot,previous_eye_)>settings_.teleport_cut_distance)
        cut(); // No temporal trail or stale collision state across sectors.
    // Non-looping cutscenes show the final frame before transitioning
    // automatically to the gameplay camera on the next update.
    if(cutscene_ && cutscene_finished_ && !cutscene_loop_)
        stop_cutscene(cutscene_exit_seconds_);
    if(cutscene_ && cutscene_playing_) {
        const float previous=cutscene_first_tick_?-1.f:cutscene_time_;
        cutscene_first_tick_=false;
        cutscene_time_+=dt*track_rate_;
        auto const duration=cutscene_->duration();
        std::vector<CameraCutsceneCue> crossed;
        if(cutscene_loop_ && duration>0.f && cutscene_time_>=duration) {
            // An event at timeline zero belongs to the new loop, not the
            // end of the previous pass. Each segment is emitted once.
            auto outgoing=cutscene_->cues_between(previous,duration);
            crossed.insert(crossed.end(),outgoing.begin(),outgoing.end());
            cutscene_time_=std::fmod(cutscene_time_,duration);
            auto incoming=cutscene_->cues_between(-1.f,cutscene_time_);
            crossed.insert(crossed.end(),incoming.begin(),incoming.end());
        } else {
            if(cutscene_time_>=duration) {
                cutscene_time_=duration;
                cutscene_finished_=true;
            }
            crossed=cutscene_->cues_between(previous,cutscene_time_);
        }
        if(!crossed.empty())
            pending_cutscene_cues_json_=serialize_camera_cues(crossed);
    }
    if(cutscene_) {
        cutscene_exposure_ev_=cutscene_->lens_at(cutscene_time_).exposure_ev;
        cutscene_lens_active_=true;
    }
    // Deterministic request arbitration; the most recently registered
    // request wins priority ties. A gameplay camera remains authoritative
    // whenever there is no active cinematic request.
    SequencedShot const* selected=nullptr;
    for(auto const& request:shots_)
        if(!selected || request.shot.priority>selected->shot.priority ||
           (request.shot.priority==selected->shot.priority &&
            request.order>selected->order))
            selected=&request;
    // Scene Camera Actors participate in the existing shot arbitration.
    // Never enqueue a persistent shot: disabling/removing a camera is immediate.
    auto const sceneShot=world?scene_camera::active_shot(*world):
        std::optional<CameraShot>{};
    CameraShot const* selectedShot=selected?&selected->shot:nullptr;
    if(sceneShot && (!selectedShot || sceneShot->priority>selectedShot->priority))
        selectedShot=&*sceneShot;
    std::string const winner=selectedShot?selectedShot->source:std::string{};
    if(winner!=active_shot_) {
        transition_from_=pose_;
        auto const blend_seconds=selectedShot?
            selectedShot->blend_seconds :
            (mode_==CameraMode::first_person?0.f:last_shot_blend_seconds_);
        if(selectedShot)
            last_shot_blend_seconds_=selectedShot->blend_seconds;
        transition_seconds_=initialized_?blend_seconds:0.f;
        transition_elapsed_=0.f;
        follow_velocity_={0.f,0.f,0.f};
        collision_limit_=-1.f;
        lookahead_offset_={0.f,0.f,0.f};
        active_shot_=winner;
    }
    CameraPose wanted;
    if(selectedShot)wanted=selectedShot->pose;
    else if(cutscene_) {
        auto frame=cutscene_->sample(cutscene_time_);
        cutscene_shot_=std::move(frame.shot);
        wanted=frame.pose;
        if(cutscene_->space()==CameraCutsceneSpace::target) {
            // Freeze the local-space origin on first camera activation.
            // Actor motion must not drag an authored cinematic shot.
            if(!cutscene_anchor_valid_) {
                cutscene_anchor_=pivot;
                cutscene_anchor_yaw_=
                    mode_==CameraMode::vehicle_chase?t.heading:t.yaw;
                cutscene_anchor_valid_=true;
            }
            const float sn=std::sin(cutscene_anchor_yaw_);
            const float cs=std::cos(cutscene_anchor_yaw_);
            const auto relative=wanted.position;
            wanted.position={
                cutscene_anchor_.x+relative.x*cs+relative.z*sn,
                cutscene_anchor_.y+relative.y,
                cutscene_anchor_.z-relative.x*sn+relative.z*cs};
            wanted.yaw=wrap(wanted.yaw+cutscene_anchor_yaw_);
        }
    } else wanted=desired(t,dt);
    try {check_pose(wanted);}
    catch(std::invalid_argument const& error) {
        throw std::invalid_argument(
            std::string("Camera pre-collision pose: ")+error.what());
    }
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
    // A first-person target sits at the eye. Its zero arm distance must not
    // squash the intermediate third-to-first dolly into an immediate cut.
    bool const has_collision = !selectedShot && !cutscene_ &&
        mode_!=CameraMode::first_person &&
        mode_!=CameraMode::free && mode_!=CameraMode::cinematic;
    bool const returning_from_third = !selectedShot && !cutscene_ &&
        mode_==CameraMode::first_person && transition_seconds_>0.f &&
        transition_source_mode_==CameraMode::third_person;
    if (has_collision && (physics || world)) {
        Vector3 const requested=wanted.position;
        if (physics)
            wanted.position=clip_physics(wanted.position);
        else {
            auto const requested_position=wanted.position;
            wanted.position=clip_line_of_sight(
                pivot,wanted.position,*world,t.ignore_object);
            if(!finite(wanted.position)) {
                std::ostringstream detail;
                detail<<"Camera fallback collision returned non-finite position"
                      <<" pivot="<<pivot.x<<","<<pivot.y<<","<<pivot.z
                      <<" requested="<<requested_position.x<<","
                      <<requested_position.y<<","<<requested_position.z
                      <<" occluders="<<world->objects.size();
                throw std::invalid_argument(detail.str());
            }
        }
        float const allowed=distance(pivot,wanted.position);
        if (!initialized_ || collision_limit_<0.f ||
            allowed<collision_limit_) {
            // Instantly move toward an occluder: no wall clipping or
            // delayed response when a fast vehicle enters a narrow gap.
            collision_limit_=allowed;
        } else {
            float const tau=settings_.collision_restore_seconds;
            float const amount=tau>0.f?1.f-std::exp(-dt/tau):1.f;
            collision_limit_=lerp(collision_limit_,allowed,amount);
        }
        float const requested_distance=distance(pivot,requested);
        if (requested_distance>1.e-6f && collision_limit_<allowed) {
            auto const ratio=collision_limit_/requested_distance;
            wanted.position={pivot.x+(requested.x-pivot.x)*ratio,
                             pivot.y+(requested.y-pivot.y)*ratio,
                             pivot.z+(requested.z-pivot.z)*ratio};
        }
    } else collision_limit_=-1.f;
    try {check_pose(wanted);}
    catch(std::invalid_argument const& error) {
        throw std::invalid_argument(std::string("Camera desired pose: ")+error.what());
    }

    bool const snap=!initialized_;
    previous_eye_=pivot;
    if (snap) {
        pose_=wanted;
        follow_pose_=wanted;
        follow_velocity_={0.f,0.f,0.f};
        transition_seconds_=transition_elapsed_=0.f;
        initialized_=true;
    } else {
        float const tau=settings_.follow_smoothing;
        float const a=tau>0.f ? 1.f-std::exp(-dt/tau) : 1.f;
        // FPS camera must exactly track gameplay orientation and eye point:
        // aiming and hitscan use the same authoritative yaw/pitch.
        if(selectedShot || cutscene_ || mode_==CameraMode::cinematic ||
           mode_==CameraMode::free) {
            // Authored cinematic tracks and free-camera poses are already
            // authoritative. Do not apply third-person follow inertia
            // when returning from a priority override.
            follow_pose_=wanted;
            follow_velocity_={0.f,0.f,0.f};
        } else if (mode_==CameraMode::first_person) {
            float const previous_fov=follow_pose_.fov_y_degrees;
            follow_pose_=wanted;
            follow_velocity_={0.f,0.f,0.f};
            // FPS aiming remains exact; only the lens FOV eases.
            follow_pose_.fov_y_degrees=lerp(previous_fov,wanted.fov_y_degrees,a);
        } else {
            CameraPose smoothed=blend(follow_pose_,wanted,a);
            float const frequency=settings_.follow_spring_frequency_hz;
            if (frequency>0.f) {
                float const omega=2.f*pi*frequency;
                // Apply the analytic spring to the previous authoritative
                // position, never to an already linearly filtered value:
                // that would make the response depend on frame subdivision.
                smoothed.position=follow_pose_.position;
                critically_damped(smoothed.position.x,follow_velocity_.x,
                    wanted.position.x,omega,dt);
                critically_damped(smoothed.position.y,follow_velocity_.y,
                    wanted.position.y,omega,dt);
                critically_damped(smoothed.position.z,follow_velocity_.z,
                    wanted.position.z,omega,dt);
            } else follow_velocity_={0.f,0.f,0.f};
            follow_pose_=smoothed;
        }
        pose_=follow_pose_;
        if (transition_seconds_>0.f) {
            transition_elapsed_+=dt;
            float const progress=std::min(1.f,transition_elapsed_/transition_seconds_);
            float const eased=progress*progress*(3.f-2.f*progress);
            pose_=blend(transition_from_,follow_pose_,eased);
            if (progress>=1.f) transition_seconds_=0.f;
        }
    }
    // Clip the actual blended dolly on TPS -> FPS to avoid crossing walls.
    // The FPS desired eye itself has no spring-arm distance to constrain.
    if ((has_collision || returning_from_third) && (physics || world)) {
        auto const attempted=pose_.position;
        if (physics)
            pose_.position=clip_physics(pose_.position);
        else
            pose_.position=clip_line_of_sight(
                pivot,pose_.position,*world,t.ignore_object);
        auto const length=distance(pivot,pose_.position);
        if (has_collision && collision_limit_>=0.f && length>collision_limit_ &&
            length>1.e-6f) {
            float const ratio=collision_limit_/length;
            pose_.position={
                pivot.x+(pose_.position.x-pivot.x)*ratio,
                pivot.y+(pose_.position.y-pivot.y)*ratio,
                pivot.z+(pose_.position.z-pivot.z)*ratio};
        }
        if (distance(attempted,pose_.position)>1.e-4f) {
            follow_velocity_={0.f,0.f,0.f}; // Contact must not leave hidden spring energy.
            if(transition_seconds_<=0.f)
                follow_pose_.position=pose_.position;
        }
    }

    CameraPose render=pose_;
    if (shake_remaining_>0.f && shake_duration_>0.f) {
        float const falloff=shake_remaining_/shake_duration_;
        float const amplitude=shake_amplitude_*falloff*falloff;
        render.position.x+=std::sin(time_*41.3f)*amplitude;
        render.position.y+=std::sin(time_*35.7f+1.1f)*amplitude*.6f;
        shake_remaining_=std::max(0.f,shake_remaining_-dt);
    }
    try {check_pose(render);}
    catch(std::invalid_argument const& error) {
        throw std::invalid_argument(std::string("Camera render pose: ")+error.what());
    }
    return render;
}

} // namespace northstarcpp
