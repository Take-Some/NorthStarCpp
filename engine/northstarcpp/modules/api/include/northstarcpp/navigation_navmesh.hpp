// NorthStarCpp — runtime physics-rasterized navigation mesh.
// Standalone, deterministic, provider-neutral planner. Walkable convex floor
// cells form a layered walkability lattice with an A* adjacency graph. No actor
// positions are mutated by planning. Collision/height sampling is injected.
#pragma once
#include <northstarcpp/scene.hpp>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <queue>
#include <stdexcept>
#include <utility>
#include <vector>

namespace northstarcpp::navigation {

struct NavSurface {
    float height=0.f;
    Vector3 normal{0.f,1.f,0.f};
};
struct NavSettings {
    float cell_size=.8f;
    float padding=5.f;
    float max_step=.48f;
    float min_up_normal=.70f;  // <=45.6 degrees
    float goal_tolerance=.65f;
    float level_spacing=3.5f;
    std::size_t max_side=72;  // bounded CPU/memory and physics query budget
};
struct NavRoute {
    enum class Status { Complete, Partial, Blocked, Invalid };
    Status status=Status::Invalid;
    std::vector<Vector3> waypoints;
    std::size_t visited=0;
    bool reached_goal() const noexcept {return status==Status::Complete;}
    bool can_advance() const noexcept {
        return (status==Status::Complete || status==Status::Partial) &&
               !waypoints.empty();
    }
};

// Samples the closest navigable collision surface below probeY.
// Implementations MUST reject steep faces and invalid/collision-free ground.
using SurfaceSampler=std::function<std::optional<NavSurface>(
    float x,float z,float probeY)>;
// Is a sweep of the agent capsule's lower portion blocked? The planner calls
// this when expanding edges, rather than assuming that points imply clearance.
using SegmentBlocked=std::function<bool(Vector3 from,Vector3 to)>;

class RuntimeNavMesh final {
public:
    explicit RuntimeNavMesh(NavSettings settings={}) : settings_(settings) {
        if(!std::isfinite(settings_.cell_size) || settings_.cell_size<.2f ||
           settings_.cell_size>4.f ||
           !std::isfinite(settings_.padding) || settings_.padding<0.f ||
           settings_.padding>30.f ||
           !std::isfinite(settings_.max_step) || settings_.max_step<=0.f ||
           settings_.max_step>2.f ||
           !std::isfinite(settings_.min_up_normal) ||
           settings_.min_up_normal<.3f || settings_.min_up_normal>1.f ||
           !std::isfinite(settings_.level_spacing) ||
           settings_.level_spacing<1.f || settings_.level_spacing>20.f ||
           settings_.max_side<8 || settings_.max_side>144)
            throw std::invalid_argument("Invalid NorthStar NavMesh settings");
    }
    std::size_t node_count() const noexcept {return nodes_.size();}
    std::size_t grid_width() const noexcept {return width_;}
    std::size_t grid_height() const noexcept {return height_;}
    bool partial_coverage() const noexcept {return partial_coverage_;}

    bool begin_build(Vector3 start,Vector3 goal) {
        nodes_.clear();cell_nodes_.clear();probes_.clear();next_cell_=0;
        width_=height_=0;partial_coverage_=false;
        if(!finite(start) || !finite(goal))return false;
        Vector3 localGoal=goal;
        const float distance=std::hypot(goal.x-start.x,goal.z-start.z);
        const float maxDistance=std::max(2.f,
            float(settings_.max_side-3)*settings_.cell_size-
            2.f*settings_.padding);
        if(distance>maxDistance) {
            const float portion=maxDistance/distance;
            localGoal.x=start.x+(goal.x-start.x)*portion;
            localGoal.z=start.z+(goal.z-start.z)*portion;
            localGoal.y=start.y+(goal.y-start.y)*portion;
            partial_coverage_=true;
        }
        min_x_=std::min(start.x,localGoal.x)-settings_.padding;
        min_z_=std::min(start.z,localGoal.z)-settings_.padding;
        max_x_=std::max(start.x,localGoal.x)+settings_.padding;
        max_z_=std::max(start.z,localGoal.z)+settings_.padding;
        width_=std::size_t(std::ceil((max_x_-min_x_)/settings_.cell_size))+1;
        height_=std::size_t(std::ceil((max_z_-min_z_)/settings_.cell_size))+1;
        if(width_>settings_.max_side || height_>settings_.max_side) {
            // Asymmetric floor rounding can exceed the budget by one cell.
            width_=std::min(width_,settings_.max_side);
            height_=std::min(height_,settings_.max_side);
        }
        if(width_>settings_.max_side || height_>settings_.max_side ||
           width_*height_>settings_.max_side*settings_.max_side)
            return false;
        cell_nodes_.resize(width_*height_);
        std::vector<float> probes{start.y,goal.y};
        if(std::abs(start.y-goal.y)>settings_.level_spacing*1.5f)
            probes.push_back((start.y+goal.y)*.5f);
        // Additional layer probing permits stacked floors near elevators,
        // overpasses and stairs, but does not invent off-mesh teleport links.
        probes.push_back(start.y+settings_.level_spacing);
        probes.push_back(start.y-settings_.level_spacing);
        if(std::abs(start.y-goal.y)>settings_.level_spacing) {
            probes.push_back(goal.y+settings_.level_spacing);
            probes.push_back(goal.y-settings_.level_spacing);
        }
        probes_=std::move(probes);
        return true;
    }

    // Rasterize at most budget cells. Caller keeps physics queries on the
    // physics owner thread; 32-96 cells/frame avoids a game-loop stall.
    bool advance_build(SurfaceSampler const& sample,std::size_t budget) {
        if(!sample || budget==0 || width_==0)return true;
        const auto end=std::min(width_*height_,next_cell_+budget);
        for(;next_cell_<end;++next_cell_) {
                const std::size_t z=next_cell_/width_,x=next_cell_%width_;
                const float px=min_x_+float(x)*settings_.cell_size;
                const float pz=min_z_+float(z)*settings_.cell_size;
                auto& bucket=cell_nodes_[z*width_+x];
                for(float probe:probes_) {
                    auto surface=sample(px,pz,probe);
                    if(!surface || !std::isfinite(surface->height) ||
                       !finite(surface->normal) ||
                       surface->normal.y<settings_.min_up_normal)
                        continue;
                    bool duplicate=false;
                    for(auto existing:bucket)
                        if(std::abs(nodes_[existing].point.y-
                                    surface->height)<settings_.max_step*.55f)
                            {duplicate=true;break;}
                    if(duplicate || bucket.size()>=4)continue;
                    bucket.push_back(nodes_.size());
                    nodes_.push_back({{px,surface->height,pz},x,z});
                }
            }
        return next_cell_==width_*height_;
    }
    bool build(Vector3 start,Vector3 goal,SurfaceSampler const& sample) {
        if(!sample || !begin_build(start,goal))return false;
        while(!advance_build(sample,256)){}
        return !nodes_.empty();
    }
    bool ready() const noexcept {return width_ && next_cell_==width_*height_;}

    NavRoute route(Vector3 start,Vector3 goal,
                   SegmentBlocked const& blocked={}) const {
        NavRoute result;
        if(!finite(start)||!finite(goal)||nodes_.empty())return result;
        auto origin=closest(start,settings_.cell_size*2.f);
        if(!origin)return result;
        auto destination=closest(goal,settings_.cell_size*2.f);
        // Partial tiles may not include the distant goal. Aim at the sampled
        // closest forward boundary rather than claiming completed navigation.
        if(!destination && partial_coverage_) {
            const float len=std::hypot(goal.x-start.x,goal.z-start.z);
            const float maxDistance=std::max(2.f,
                float(settings_.max_side-3)*settings_.cell_size-
                2.f*settings_.padding);
            if(len>maxDistance) {
                auto target=start;
                target.x+=(goal.x-start.x)*maxDistance/len;
                target.z+=(goal.z-start.z)*maxDistance/len;
                target.y+=(goal.y-start.y)*maxDistance/len;
                destination=closest(target,settings_.cell_size*3.f);
            }
        }
        if(!destination) {
            result.status=NavRoute::Status::Blocked;return result;
        }
        const std::size_t n=nodes_.size();
        const auto infinity=std::numeric_limits<float>::infinity();
        std::vector<float> g(n,infinity);
        std::vector<std::size_t> predecessor(n,n);
        std::vector<unsigned char> closed(n,0);
        using QueueEntry=std::pair<float,std::size_t>;
        std::priority_queue<QueueEntry,std::vector<QueueEntry>,
            std::greater<QueueEntry>> open;
        auto heuristic=[&](std::size_t a)->float {
            const auto& p=nodes_[a].point;
            const auto& d=nodes_[*destination].point;
            return std::hypot(p.x-d.x,p.z-d.z);
        };
        g[*origin]=0.f;open.push({heuristic(*origin),*origin});
        std::size_t expanded=0;
        while(!open.empty() && expanded<=n) {
            const auto current=open.top().second;open.pop();
            if(closed[current])continue;
            closed[current]=1;++expanded;
            if(current==*destination)break;
            auto const& here=nodes_[current];
            for(int dz=-1;dz<=1;++dz)for(int dx=-1;dx<=1;++dx) {
                if(!dx&&!dz)continue;
                const auto nx=int(here.x)+dx,nz=int(here.z)+dz;
                if(nx<0||nz<0||nx>=int(width_)||nz>=int(height_))continue;
                auto const& adjacent=cell_nodes_[std::size_t(nz)*width_+std::size_t(nx)];
                for(auto next:adjacent) {
                    if(closed[next] || !can_connect(current,next,blocked))
                        continue;
                    // No diagonal shortcuts through the corners of two walls.
                    if(dx&&dz &&
                       (!cardinal_exists(current,int(here.x)+dx,int(here.z)) ||
                        !cardinal_exists(current,int(here.x),int(here.z)+dz)))
                        continue;
                    auto const& to=nodes_[next].point;
                    const float step=std::hypot(to.x-here.point.x,
                                                to.z-here.point.z)+
                                                .15f*std::abs(to.y-here.point.y);
                    const float candidate=g[current]+step;
                    if(candidate>=g[next])continue;
                    g[next]=candidate;
                    predecessor[next]=current;
                    open.push({candidate+heuristic(next),next});
                }
            }
        }
        result.visited=expanded;
        if(!closed[*destination]) {
            result.status=NavRoute::Status::Blocked;return result;
        }
        std::vector<Vector3> backwards;
        for(std::size_t at=*destination;at!=*origin;) {
            backwards.push_back(nodes_[at].point);
            at=predecessor[at];
            if(at>=n) {
                result.status=NavRoute::Status::Blocked;return result;
            }
        }
        std::reverse(backwards.begin(),backwards.end());
        // Prune collinear center samples but preserve corners and level changes.
        std::vector<Vector3> simplified;
        for(std::size_t i=0;i<backwards.size();++i) {
            if(i+1<backwards.size() && i>0) {
                const auto& before=backwards[i-1];
                const auto& after=backwards[i+1];
                const auto& current=backwards[i];
                const float ax=current.x-before.x,az=current.z-before.z;
                const float bx=after.x-current.x,bz=after.z-current.z;
                if(std::abs(ax*bz-az*bx)<.0001f &&
                   std::abs(current.y-before.y)<.06f &&
                   std::abs(after.y-current.y)<.06f)
                    continue;
            }
            simplified.push_back(backwards[i]);
        }
        const auto end=nodes_[*destination].point;
        const bool completed=!partial_coverage_ &&
            std::hypot(end.x-goal.x,end.z-goal.z)<=
                settings_.cell_size*1.8f &&
            std::abs(end.y-goal.y)<=std::max(settings_.max_step,1.2f);
        if(completed && !simplified.empty())simplified.back()=goal;
        result.status=completed?NavRoute::Status::Complete:
                                NavRoute::Status::Partial;
        result.waypoints=std::move(simplified);
        return result;
    }
private:
    struct Node {Vector3 point;std::size_t x,z;};
    static bool finite(Vector3 v) {
        return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);
    }
    std::optional<std::size_t> closest(Vector3 point,float allowed) const {
        std::optional<std::size_t> best;
        float score=std::numeric_limits<float>::infinity();
        for(std::size_t i=0;i<nodes_.size();++i) {
            const auto& p=nodes_[i].point;
            const float horiz=std::hypot(p.x-point.x,p.z-point.z);
            if(horiz>allowed || std::abs(p.y-point.y)>2.0f)continue;
            const float cost=horiz+.7f*std::abs(p.y-point.y);
            if(cost<score){score=cost;best=i;}
        }
        return best;
    }
    bool cardinal_exists(std::size_t origin,int x,int z) const {
        if(x<0||z<0||x>=int(width_)||z>=int(height_))return false;
        auto const& p=nodes_[origin].point;
        for(auto index:cell_nodes_[std::size_t(z)*width_+std::size_t(x)])
            if(std::abs(nodes_[index].point.y-p.y)<=settings_.max_step)
                return true;
        return false;
    }
    bool can_connect(std::size_t from,std::size_t to,
                     SegmentBlocked const& blocked) const {
        auto const& a=nodes_[from].point;
        auto const& b=nodes_[to].point;
        if(std::abs(a.y-b.y)>settings_.max_step)return false;
        if(!blocked)return true;
        // Sweep at the lower capsule portion with clearance, so thin fences
        // and walls between sample centers cannot be ignored.
        return !blocked({a.x,a.y+.55f,a.z},{b.x,b.y+.55f,b.z});
    }
    NavSettings settings_;
    float min_x_=0.f,min_z_=0.f,max_x_=0.f,max_z_=0.f;
    std::size_t width_=0,height_=0;
    bool partial_coverage_=false;
    std::vector<Node> nodes_;
    std::vector<float> probes_;
    std::size_t next_cell_=0;
    std::vector<std::vector<std::size_t>> cell_nodes_;
};
} // namespace northstarcpp::navigation
