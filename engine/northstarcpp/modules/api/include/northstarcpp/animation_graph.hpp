#pragma once
#include <northstarcpp/skeleton.hpp>
#include <northstarcpp/asset_streaming.hpp>
#include <cstdint>
#include <future>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
namespace northstarcpp {
class AssetStore;
struct AnimationBlendPoint { float at=0.f; std::string clip; };
struct AnimationGraphState {
    std::string name,clip,parameter,sync_group;
    std::vector<AnimationBlendPoint> points;
};
enum class AnimationTransitionOp { greater_equal, less_equal };
struct AnimationGraphTransition {
    std::string from,to,parameter;
    AnimationTransitionOp operation=AnimationTransitionOp::greater_equal;
    float threshold=0.f,fade_seconds=.15f;
};
enum class AnimationLayerMode { override_pose,additive };
struct AnimationGraphLayer {
    std::string name,clip,parameter;
    float weight=1.f;
    AnimationLayerMode mode=AnimationLayerMode::override_pose;
    std::vector<std::string> bone_roots;
};
struct AnimationGraphMarker { std::string clip,name; float normalized_time=0.f; };
struct AnimationGraph {
    std::string initial;
    std::vector<AnimationGraphState> states;
    std::vector<AnimationGraphTransition> transitions;
    std::vector<AnimationGraphLayer> layers;
    std::vector<AnimationGraphMarker> markers;
    std::unordered_map<std::string,float> defaults;
    std::vector<std::string> references() const;
};
AnimationGraph parse_animation_graph_xml(std::string const& xml);
AnimationGraph load_animation_graph(AssetStore const&,std::string const& path);
class AnimationGraphInstance {
public:
    struct Event {std::string name,clip;std::uint64_t sequence=0;};
    AnimationGraphInstance(Skeleton const& rig,AnimationGraph graph);
    void bind_clip(std::string const&,std::shared_ptr<SkeletalClip const>);
    bool ready() const noexcept;
    void set_parameter(std::string const&,float);
    float parameter(std::string const&) const;
    std::string const& state() const noexcept;
    void advance(float dt);
    void evaluate(SkeletonPose&);
    std::vector<Event> const& events() const noexcept {return events_;}
    std::vector<Event> drain_events();
    std::uint64_t dropped_events() const noexcept {return dropped_events_;}
private:
    Skeleton const* rig_;
    AnimationGraph graph_;
    std::vector<std::string> required_refs_;
    std::unordered_map<std::string,std::shared_ptr<SkeletalClip const>> clips_;
    std::unordered_map<std::string,float> values_;
    std::vector<std::vector<std::uint8_t>> masks_;
    std::vector<JointTransform> pose_,scratch_,scratch_b_,previous_;
    // The last animation-only locals for unkeyed joints and async streaming.
    // Never blend a graph transition through the imported bind/T-pose.
    std::vector<JointTransform> last_evaluated_pose_;
    bool has_last_evaluated_pose_=false;
    std::vector<std::uint8_t> keyed_,keyed_a_,keyed_b_,keyed_old_;
    std::vector<Event> events_;
    std::size_t active_=0,old_=0;
    float clock_=0.f,old_clock_=0.f,fade_elapsed_=0.f,fade_duration_=0.f;
    std::uint64_t sequence_=0,dropped_events_=0;
    bool transitioning_=false;
    float get(std::string const&) const noexcept;
    void evaluate_state(std::size_t,float,std::vector<JointTransform>&,
                        std::vector<std::uint8_t>&);
    void evaluate_clip(std::string const&,float,
                       std::vector<JointTransform>&,std::vector<std::uint8_t>&);
    float duration(std::string const&) const;
    void emit_notifies(std::size_t,float,float);
};
class AsyncYcdClipLoader {
public:
    AsyncYcdClipLoader(AssetStore const&,std::shared_ptr<Skeleton const>,
                       std::size_t max_decoders=2);
    ~AsyncYcdClipLoader();
    AsyncYcdClipLoader(AsyncYcdClipLoader const&)=delete;
    AsyncYcdClipLoader& operator=(AsyncYcdClipLoader const&)=delete;
    void request(std::string reference,AssetPriority priority=AssetPriority::normal);
    void poll();
    std::shared_ptr<SkeletalClip const> find(std::string const&) const;
    std::string error(std::string const&) const;
    std::size_t pending() const noexcept;
private:
    struct Entry {
        std::string path,name,error;
        AssetRequest<std::vector<std::uint8_t>> request;
        std::future<std::shared_ptr<SkeletalClip const>> decoding;
        std::shared_ptr<SkeletalClip const> ready;
        bool failed=false;
    };
    AssetStore const* assets_;
    std::shared_ptr<Skeleton const> rig_;
    std::size_t max_decoders_;
    std::unordered_map<std::string,Entry> entries_;
};
}
