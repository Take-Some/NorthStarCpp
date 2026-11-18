#pragma once
// Internal, strongly typed domain callback tables. No App type is exposed.
// Compile-time method sets reject missing methods or accidental API drift.
#include <northstarcpp/scripting.hpp>
#include "window_script_lifetime.hpp"
#include <northstarcpp/input.hpp>
#include <northstarcpp/input_actions.hpp>
#include <northstarcpp/audio.hpp>
#include <northstarcpp/scene.hpp>
#include <northstarcpp/ecs.hpp>
#include <northstarcpp/physics.hpp>
#include <northstarcpp/player_action_events.hpp>
// Viewport is forward-declared below; the Scene implementation includes viewport.hpp.
#include <unordered_map>
#include <optional>
#include <northstarcpp/camera.hpp>
#include <northstarcpp/assets.hpp>
#include <northstarcpp/fps_assets.hpp>
#include <northstarcpp/nac.hpp>
#include <vector>
#include <functional>
#include <memory>
#include <algorithm>
#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace northstarcpp { class Viewport; }

namespace northstarcpp::window_app_internal {

// Public V8 API grouping lives at the binding edge. Implementations and
// typed domain callback tables retain their strongly typed C++ contracts.
// No legacy flat bindings are published by the game runtime.
inline std::string_view script_capability_group(
        std::string_view capability,std::string_view method) noexcept {
    if(capability=="engine.scene") {
        if(method=="spawn" || method=="destroy" || method=="objectCount" ||
           method=="objects" || method=="setLayerVisible")return "engine.scene.entities";
        if(method=="moveRuntime" || method=="rotateRuntime" ||
           method=="moveStatic" || method=="rotateStatic" ||
           method=="setColor")return "engine.scene.transforms";
        return "engine.scene.animation";
    }
    if(capability=="engine.camera") {
        if(method=="available" || method=="setMode" || method=="mode" ||
           method=="setFollowDistance" || method=="followDistance" ||
           method=="setAngles" || method=="angles" || method=="setZoom")
            return "engine.camera.views";
        if(method=="shake" || method=="recoil")return "engine.camera.effects";
        if(method=="playCutscene" || method=="stopCutscene" ||
           method=="pauseCutscene" || method=="seekCutscene" ||
           method=="cutsceneState" || method=="lensState" ||
           method=="playTrack")return "engine.camera.cinematics";
        return "engine.camera.shots";
    }
    if(capability=="engine.ecs")return "engine.ecs.entities";
    if(capability=="engine.input") {
        if(method=="isDown")return "engine.input.keyboard";
        if(method=="mouseDown" || method=="pointerDelta" ||
           method=="wheelDelta")return "engine.input.pointer";
        if(method=="gamepad")return "engine.input.gamepads";
        return "engine.input.actions";
    }
    if(capability=="engine.player") {
        if(method=="viewTarget" || method=="setViewTarget" ||
           method=="resetViewTarget")return "engine.player.view";
        if(method=="controlledEntity" || method=="controlledIndex" ||
           method=="possess" || method=="unpossess")return "engine.player.controller";
        return "engine.player.motion";
    }
    if(capability=="engine.audio") {
        if(method=="play" || method=="playNac")return "engine.audio.playback";
        if(method=="setVoicePosition")return "engine.audio.spatial";
        return "engine.audio.voices";
    }
    return capability;
}

template<class Tag>
class TypedScriptCallbacks {
public:
    static constexpr std::size_t count = Tag::names.size();
    void add(std::string_view method, ScriptRuntime::ScriptHandler handler) {
        auto const it=std::find(Tag::names.begin(),Tag::names.end(),method);
        if(it==Tag::names.end())
            throw std::invalid_argument("Unknown scripted method in "+std::string(Tag::capability));
        auto const index=static_cast<std::size_t>(it-Tag::names.begin());
        if(callbacks_[index])
            throw std::invalid_argument("Duplicate scripted method in "+std::string(Tag::capability));
        if(!handler)throw std::invalid_argument("Empty scripted method handler");
        callbacks_[index]=std::move(handler);
    }
    bool complete() const noexcept {
        return std::all_of(callbacks_.begin(),callbacks_.end(),
                           [](auto const& callback) { return static_cast<bool>(callback); });
    }
    void set_lifetime(std::shared_ptr<ScriptBindingLease> const& lease) noexcept {
        lifetime_=lease;
        guarded_=true;
    }
    void bind_all(ScriptRuntime& runtime) && {
        for(std::size_t i=0;i<callbacks_.size();++i) {
            if(!callbacks_[i])
                throw std::runtime_error(
                    "Missing "+std::string(Tag::capability)+"."+std::string(Tag::names[i])+" binding");
            auto callback=std::move(callbacks_[i]);
            if(guarded_) {
                // Weak token prevents callback storage keeping a dead host alive.
                runtime.bind(std::string(script_capability_group(Tag::capability,Tag::names[i])),
                    std::string(Tag::names[i]),
                    [lease=lifetime_,callback=std::move(callback)](std::string_view json) {
                        auto token=lease.lock();
                        if(!token || !token->valid())
                            throw std::out_of_range("Stale ScriptRuntime binding context");
                        return callback(json);
                    });
            } else {
                runtime.bind(std::string(script_capability_group(Tag::capability,Tag::names[i])),
                    std::string(Tag::names[i]),
                             std::move(callback));
            }
        }
    }
private:
    std::array<ScriptRuntime::ScriptHandler,count> callbacks_{};
    std::weak_ptr<ScriptBindingLease> lifetime_;
    bool guarded_=false;
};

struct SceneScriptTag final {
    static constexpr std::string_view capability="engine.scene";
    inline static constexpr std::array<std::string_view,15> names{{
        "spawn",
        "destroy",
        "moveRuntime",
        "rotateRuntime",
        "objectCount",
        "setLayerVisible",
        "objects",
        "moveStatic",
        "rotateStatic",
        "setColor",
        "setAnimation",
        "setVisualMode",
        "attachBone",
        "attachmentPoint",
        "detachBone"
    }};
};
using SceneScriptBindings=TypedScriptCallbacks<SceneScriptTag>;

struct CameraScriptTag final {
    static constexpr std::string_view capability="engine.camera";
    inline static constexpr std::array<std::string_view,23> names{{
        "available",
        "setMode",
        "mode",
        "setFollowDistance",
        "followDistance",
        "setAngles",
        "angles",
        "cut",
        "pushShot",
        "clearShot",
        "clearShots",
        "activeShot",
        "applyPreset",
        "shake",
        "recoil",
        "playCutscene",
        "stopCutscene",
        "pauseCutscene",
        "seekCutscene",
        "cutsceneState",
        "lensState",
        "playTrack",
        "setZoom"
    }};
};
using CameraScriptBindings=TypedScriptCallbacks<CameraScriptTag>;

struct EcsScriptTag final {
    static constexpr std::string_view capability="engine.ecs";
    inline static constexpr std::array<std::string_view,8> names{{
        "entities",
        "queryMany",
        "query",
        "has",
        "get",
        "getScript",
        "getPosition",
        "setPosition"
    }};
};
using EcsScriptBindings=TypedScriptCallbacks<EcsScriptTag>;

struct InputScriptTag final {
    static constexpr std::string_view capability="engine.input";
    inline static constexpr std::array<std::string_view,8> names{{
        "isDown",
        "actionDown",
        "rebindAction",
        "setContextEnabled",
        "mouseDown",
        "pointerDelta",
        "wheelDelta",
        "gamepad"
    }};
};
using InputScriptBindings=TypedScriptCallbacks<InputScriptTag>;

struct PlayerScriptTag final {
    static constexpr std::string_view capability="engine.player";
    inline static constexpr std::array<std::string_view,16> names{{
        "controlledEntity",
        "viewTarget",
        "setViewTarget",
        "resetViewTarget",
        "controlledIndex",
        "animationState",
        "possess",
        "unpossess",
        "sprint",
        "move",
        "motion",
        "motionState",
        "ground",
        "jump",
        "eye",
        "setEyeHeight"
    }};
};
using PlayerScriptBindings=TypedScriptCallbacks<PlayerScriptTag>;

struct AudioScriptTag final {
    static constexpr std::string_view capability="engine.audio";
    inline static constexpr std::array<std::string_view,6> names{{
        "playNac",
        "supportsVoices",
        "stopVoice",
        "setVoiceGain",
        "setVoicePosition",
        "play"
    }};
};
using AudioScriptBindings=TypedScriptCallbacks<AudioScriptTag>;


// Domain-specific views expose only what a native callback needs. These
// App-free adapters hold no Win32/App field, and are not part of engine ABI.
struct SceneCountStateView final {
    std::function<std::size_t()> object_count;
};
struct CameraAvailableStateView final {
    std::function<bool()> available;
};
struct EcsEntitiesStateView final {
    std::function<Scene const*()> current_scene;
    std::function<bool(std::size_t)> index_is_live;
};
struct PlayerMotionStateView final {
    float const* move_x=nullptr;
    float const* move_z=nullptr;
};
struct AudioVoicesStateView final {
    std::unique_ptr<AudioEngine>* voice_audio=nullptr;
};
void collect_scene_count_script_binding(SceneScriptBindings&,SceneCountStateView);
void collect_camera_available_script_binding(CameraScriptBindings&,CameraAvailableStateView);
void collect_ecs_entities_script_binding(EcsScriptBindings&,EcsEntitiesStateView);
void collect_player_motion_script_binding(PlayerScriptBindings&,PlayerMotionStateView);
void collect_audio_voices_script_binding(AudioScriptBindings&,AudioVoicesStateView);

// App-free live camera capability.
struct CameraDomainCapabilities final {
    std::unique_ptr<CameraDirector> const* camera_slot=nullptr;
    std::unique_ptr<AssetStore> const* assets_slot=nullptr;
    bool const* first_person=nullptr;
    float* yaw=nullptr;
    float* pitch=nullptr;
    CameraDirector* camera() const noexcept {
        return camera_slot ? camera_slot->get() : nullptr;
    }
    AssetStore* assets() const noexcept {
        return assets_slot ? assets_slot->get() : nullptr;
    }
};
void collect_camera_runtime_script_bindings(CameraScriptBindings&,CameraDomainCapabilities);

// Narrow live ECS capability. No App fields or App pointer cross this ABI.
struct EcsDomainCapabilities final {
    std::unique_ptr<PhysicsScene> const* physics_slot=nullptr;
    std::unique_ptr<EcsWorld> const* ecs_slot=nullptr;
    std::unordered_map<std::uint64_t,std::size_t> const* indices=nullptr;
    std::unordered_map<NsEntity,std::uint64_t> const* handles=nullptr;
    std::uint64_t const* scene_generation=nullptr;
    std::uint64_t captured_scene_generation=0;
    void require_scene() const {
        if(!scene_generation || *scene_generation!=captured_scene_generation)
            throw std::out_of_range("Stale scene generation");
    }
    PhysicsScene* physics() const {
        require_scene();
        return physics_slot ? physics_slot->get() : nullptr;
    }
    EcsWorld* ecs() const {
        require_scene();
        return ecs_slot ? ecs_slot->get() : nullptr;
    }
    auto const& entity_indices() const {return *indices;}
    auto const& handle_ids() const {return *handles;}
    std::optional<std::size_t> find_live_index(std::uint64_t id) const {
        auto* world=ecs();
        auto* scene=physics();
        if(!id || !indices || !world || !scene)return std::nullopt;
        auto it=indices->find(id);
        if(it==indices->end())return std::nullopt;
        auto idx=it->second;
        auto const& objects=scene->scene().objects;
        if(idx>=objects.size() || idx>=world->size() ||
           objects[idx].node.id!=id ||
           (idx>=scene->authored_object_count() && !scene->runtime_alive(idx)))
            return std::nullopt;
        return idx;
    }
    std::size_t require_live_index(std::uint64_t id) const {
        auto index=find_live_index(id);
        if(!index)throw std::out_of_range("Stale or unknown scene entity ID");
        return *index;
    }
    bool live_handle(NsEntity handle,std::uint64_t id) const {
        auto idx=find_live_index(id);
        return idx && ecs()->at(*idx)==handle;
    }
};
void collect_ecs_runtime_script_bindings(EcsScriptBindings&,EcsDomainCapabilities);

// Live, scene-owned capabilities, independent of App and Win32.
// Viewport is declared in northstarcpp by viewport.hpp.
struct SceneDomainCapabilities final {
    std::unique_ptr<PhysicsScene> const* physics_slot=nullptr;
    std::unique_ptr<EcsWorld> const* ecs_slot=nullptr;
    std::unique_ptr<AssetStore> const* assets_slot=nullptr;
    std::unique_ptr<Viewport> const* graphics_slot=nullptr;
    std::unique_ptr<CameraDirector> const* camera_slot=nullptr;
    CameraPose const* camera_pose=nullptr;
    std::unordered_map<std::uint64_t,std::size_t>* indices=nullptr;
    std::unordered_map<NsEntity,std::uint64_t>* handles=nullptr;
    std::unordered_map<std::uint64_t,NsEntity>* spawn_handles_slot=nullptr;
    std::unordered_map<std::uint64_t,std::uint64_t>* owners=nullptr;
    std::uint64_t* next_id=nullptr;
    std::unordered_map<std::size_t,std::uint32_t>* colors=nullptr;
    std::unordered_map<std::uint64_t,std::string>* animations=nullptr;
    std::unordered_map<std::uint64_t,float>* animation_rates=nullptr;
    std::unordered_map<std::uint64_t,std::string>* visual_modes=nullptr;
    std::unordered_map<std::uint64_t,std::string>* attachments=nullptr;
    std::uint64_t owner=0;
    std::uint64_t const* scene_generation=nullptr;
    std::uint64_t captured_scene_generation=0;
    std::function<void(std::uint64_t,std::uint64_t)> destroy_entity;
    std::function<bool()> layer_available;
    std::function<void(std::string const&,bool)> set_layer_visible;
    void require_scene() const {
        if(!scene_generation || *scene_generation!=captured_scene_generation)
            throw std::out_of_range("Stale script scene generation");
    }
    PhysicsScene* physics() const {require_scene();return physics_slot?physics_slot->get():nullptr;}
    EcsWorld* ecs() const {require_scene();return ecs_slot?ecs_slot->get():nullptr;}
    AssetStore* assets() const {require_scene();return assets_slot?assets_slot->get():nullptr;}
    Viewport* graphics() const {require_scene();return graphics_slot?graphics_slot->get():nullptr;}
    CameraDirector* camera() const {require_scene();return camera_slot?camera_slot->get():nullptr;}
    auto& entity_indices() const {require_scene();return *indices;}
    auto& handle_ids() const {require_scene();return *handles;}
    auto& spawn_handles() const {require_scene();return *spawn_handles_slot;}
    auto& spawn_owners() const {require_scene();return *owners;}
    auto& object_colors() const {require_scene();return *colors;}
    auto& model_animations() const {require_scene();return *animations;}
    auto& model_animation_rates() const {require_scene();return *animation_rates;}
    auto& model_visual_modes() const {require_scene();return *visual_modes;}
    auto& model_bone_attachments() const {require_scene();return *attachments;}
    std::size_t live_index(std::uint64_t id) const {
        auto const& map=entity_indices();
        auto it=map.find(id);
        if(it==map.end() || !physics())
            throw std::out_of_range("Stale script scene entity ID");
        auto index=it->second;
        auto* world=physics();
        if(index>=world->scene().objects.size() ||
           world->scene().objects[index].node.id!=id ||
           (index>=world->authored_object_count() && !world->runtime_alive(index)))
            throw std::out_of_range("Stale script scene entity ID");
        return index;
    }
};
void collect_scene_runtime_script_bindings(SceneScriptBindings&,SceneDomainCapabilities);

// Player-only live state, no App or Win32 implementation detail.
struct PlayerDomainCapabilities final {
    std::unique_ptr<PhysicsScene> const* physics_slot=nullptr;
    std::unique_ptr<CameraDirector> const* camera_slot=nullptr;
    bool const* scripted_game=nullptr;
    CameraSettings* camera_settings=nullptr;
    float* eye_height=nullptr;
    float* move_x=nullptr;
    float* move_z=nullptr;
    player_action_events::Tracker const* motion_tracker=nullptr;
    std::uint64_t const* scene_generation=nullptr;
    std::uint64_t captured_scene_generation=0;
    void require_scene() const {
        if(!scene_generation || *scene_generation!=captured_scene_generation)
            throw std::out_of_range("Stale Player scene generation");
    }
    PhysicsScene* physics() const {require_scene();return physics_slot?physics_slot->get():nullptr;}
    CameraDirector* camera() const {require_scene();return camera_slot?camera_slot->get():nullptr;}
};
void collect_player_runtime_script_bindings(PlayerScriptBindings&,PlayerDomainCapabilities);

// Audio-only live resources; lazily create the same provider instances as
// before without capturing the complete host object.
struct AudioDomainCapabilities final {
    std::unique_ptr<AssetStore> const* assets_slot=nullptr;
    std::unique_ptr<AssetManager> const* fps_assets_slot=nullptr;
    std::unordered_map<std::string,std::vector<nac::Track>>* nac_cache_slot=nullptr;
    std::unordered_map<std::string,FpsAudioClip>* clip_cache_slot=nullptr;
    std::unique_ptr<AudioEngine>* voice_audio_slot=nullptr;
    std::unique_ptr<AudioOutput>* output_slot=nullptr;
    std::uint64_t const* scene_generation=nullptr;
    std::uint64_t captured_scene_generation=0;
    void require_scene() const {
        if(!scene_generation || *scene_generation!=captured_scene_generation)
            throw std::out_of_range("Stale Audio scene generation");
    }
    AssetStore* assets() const {require_scene();return assets_slot?assets_slot->get():nullptr;}
    AssetManager* fps_assets() const {require_scene();return fps_assets_slot?fps_assets_slot->get():nullptr;}
    auto& nac_cache() const {require_scene();return *nac_cache_slot;}
    auto& clip_cache() const {require_scene();return *clip_cache_slot;}
    auto& voice_audio() const {require_scene();return *voice_audio_slot;}
    auto& output() const {require_scene();return *output_slot;}
};
void collect_audio_runtime_script_bindings(AudioScriptBindings&,AudioDomainCapabilities);

struct ScriptDomainBindings final {
    void set_lifetime(std::shared_ptr<ScriptBindingLease> const& lease) noexcept {
        scene.set_lifetime(lease);
        camera.set_lifetime(lease);
        ecs.set_lifetime(lease);
        input.set_lifetime(lease);
        player.set_lifetime(lease);
        audio.set_lifetime(lease);
    }
    SceneScriptBindings scene;
    CameraScriptBindings camera;
    EcsScriptBindings ecs;
    InputScriptBindings input;
    PlayerScriptBindings player;
    AudioScriptBindings audio;
};

void register_scene_script_bindings(ScriptRuntime&, SceneScriptBindings&&);
void register_camera_script_bindings(ScriptRuntime&, CameraScriptBindings&&);
void register_ecs_script_bindings(ScriptRuntime&, EcsScriptBindings&&);
void register_input_script_bindings(ScriptRuntime&, InputScriptBindings&&);
void register_player_script_bindings(ScriptRuntime&, PlayerScriptBindings&&);
void register_audio_script_bindings(ScriptRuntime&, AudioScriptBindings&&);

void bind_script_domains(ScriptRuntime&,ScriptDomainBindings&&);

// Read-only, host-owned input view. The ScriptRuntime and host callbacks never
// outlive the App session that owns these pointers. No pointer is exported to JS.
struct InputBindingStateView final {
    gameplay::InputActionMap* actions=nullptr;
    std::function<void()> save_actions;
    bool const* script_paused=nullptr;
    bool const* menu_visible=nullptr;
    std::uint8_t const* keys=nullptr;
    std::size_t key_count=0;
    std::uint8_t const* mouse_buttons=nullptr;
    std::size_t mouse_button_count=0;
    float const* pointer_dx=nullptr;
    float const* pointer_dy=nullptr;
    std::int32_t const* wheel_vertical=nullptr;
    NorthStarInputGamepad const* gamepads=nullptr;
    std::size_t gamepad_count=0;
};
void collect_input_script_bindings(InputScriptBindings&,InputBindingStateView);

} // namespace northstarcpp::window_app_internal
