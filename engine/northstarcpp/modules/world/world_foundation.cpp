// Copyright (c) 2026 Kayla's Systems. Native terrain/CityRoot math foundation.
#include <northstarcpp/world_foundation.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>
#include <utility>

namespace northstarcpp::world {
namespace {
constexpr double pi=3.14159265358979323846264338327950288;
bool finite(Vec3d p) noexcept {
    return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);
}
bool power2(std::uint32_t x) noexcept {return x && (x & (x-1u))==0;}
std::uint32_t hash2(std::uint32_t seed,std::int64_t ix,std::int64_t iz) noexcept {
    auto h=seed ^
        (static_cast<std::uint32_t>(ix)*0x9e3779b1u) ^
        (static_cast<std::uint32_t>(iz)*0x85ebca6bu);
    h^=h>>16; h*=0x7feb352du; h^=h>>15; h*=0x846ca68bu; h^=h>>16;
    return h;
}
double smooth(double t) noexcept{return t*t*(3.0-2.0*t);}
double value_noise(std::uint32_t seed,double x,double z,double scale) {
    const auto px=x/scale,pz=z/scale;
    const auto ix=static_cast<std::int64_t>(std::floor(px));
    const auto iz=static_cast<std::int64_t>(std::floor(pz));
    const double tx=smooth(px-static_cast<double>(ix));
    const double tz=smooth(pz-static_cast<double>(iz));
    auto sample=[&](std::int64_t a,std::int64_t b) {
        return static_cast<double>(hash2(seed,a,b))/
            static_cast<double>(std::numeric_limits<std::uint32_t>::max());
    };
    const auto a=sample(ix,iz),b=sample(ix+1,iz),
               c=sample(ix,iz+1),d=sample(ix+1,iz+1);
    return (a+(b-a)*tx)*(1.0-tz)+(c+(d-c)*tx)*tz;
}
std::uint32_t vertex(std::uint32_t ix,std::uint32_t iz,std::uint32_t n) {
    return iz*(n+1)+ix;
}
Vec3d plus(Vec3d a,Vec3d b) noexcept {return {a.x+b.x,a.y+b.y,a.z+b.z};}
Vec3d minus(Vec3d a,Vec3d b) noexcept {return {a.x-b.x,a.y-b.y,a.z-b.z};}
std::array<float,16> matrix(double yaw,Vec3d position) {
    if(!finite(position)||!std::isfinite(yaw)||
       std::abs(position.x)>1e9 || std::abs(position.y)>1e9 ||
       std::abs(position.z)>1e9)throw std::invalid_argument("Invalid GPU instance pose");
    const float c=static_cast<float>(std::cos(yaw)),
                s=static_cast<float>(std::sin(yaw));
    return {c,0.f,-s,0.f, 0.f,1.f,0.f,0.f,
            s,0.f,c,0.f, static_cast<float>(position.x),
            static_cast<float>(position.y),static_cast<float>(position.z),1.f};
}
} // namespace
void TerrainConfig::validate() const {
    if(!power2(quads_per_axis) || quads_per_axis<8 || quads_per_axis>128 ||
       !power2(collision_quads) || collision_quads<4 || collision_quads>128 ||
       radius<1 || radius>6 || max_lod>5 ||
       (quads_per_axis>>max_lod)<2 ||
       !std::isfinite(width_m) || width_m<8 || width_m>1024 ||
       !std::isfinite(vertical_scale_m) || vertical_scale_m<0 ||
       vertical_scale_m>1024 || !std::isfinite(base_height_m) ||
       std::abs(base_height_m)>10000 ||
       !std::isfinite(skirt_depth_m) || skirt_depth_m<0.25 ||
       skirt_depth_m>100)
        throw std::invalid_argument("Invalid bounded TerrainConfig");
}
TerrainProvider::TerrainProvider(TerrainConfig config):config_(config) {
    config_.validate();
}
double TerrainProvider::height(double world_x,double world_z) const {
    if(!std::isfinite(world_x)||!std::isfinite(world_z)||
       std::abs(world_x)>1e7||std::abs(world_z)>1e7)
        throw std::out_of_range("Terrain coordinate outside noise grid");
    const auto seed=config_.seed;
    // Multi-octave seeded, continuous world-space height function.
    const double broad=value_noise(seed,world_x,world_z,336.0);
    const double medium=value_noise(seed^0xa78be429u,world_x,world_z,112.0);
    const double detail=value_noise(seed^0x41c64e6du,world_x,world_z,37.0);
    return config_.base_height_m+
        config_.vertical_scale_m*(0.58*(broad-0.5)+
                                  0.29*(medium-0.5)+0.13*(detail-0.5));
}
Vec3f TerrainProvider::normal(double x,double z) const {
    constexpr double eps=0.2;
    const double dx=(height(x+eps,z)-height(x-eps,z))/(2*eps);
    const double dz=(height(x,z+eps)-height(x,z-eps))/(2*eps);
    const double length=std::sqrt(1.0+dx*dx+dz*dz);
    return {static_cast<float>(-dx/length),static_cast<float>(1.0/length),
            static_cast<float>(-dz/length)};
}
TerrainChunk TerrainProvider::generate(ChunkKey key,std::uint32_t lod) const {
    if(lod>config_.max_lod || std::abs(static_cast<std::int64_t>(key.x))>50000 ||
       std::abs(static_cast<std::int64_t>(key.z))>50000)
        throw std::out_of_range("Terrain chunk coordinate or LOD");
    TerrainChunk chunk;
    chunk.key=key;
    chunk.lod=lod;
    const auto w=config_.width_m;
    const double ox=static_cast<double>(key.x)*w;
    const double oz=static_cast<double>(key.z)*w;
    chunk.anchor={ox,0.,oz};
    auto const n=config_.quads_per_axis>>lod;
    auto const h=w/static_cast<double>(n);
    auto& render=chunk.render;
    render.vertices.reserve((n+1)*(n+1)+4*(n+1));
    render.indices.reserve(6*n*n+24*n);
    for(std::uint32_t iz=0;iz<=n;iz++){
        for(std::uint32_t ix=0;ix<=n;ix++){
            const double lx=ix*h,lz=iz*h;
            render.vertices.push_back({
                {static_cast<float>(lx),
                 static_cast<float>(height(ox+lx,oz+lz)),
                 static_cast<float>(lz)},
                normal(ox+lx,oz+lz),
                static_cast<float>((ox+lx)/16.),
                static_cast<float>((oz+lz)/16.)
            });
        }
    }
    for(std::uint32_t iz=0;iz<n;iz++)for(std::uint32_t ix=0;ix<n;ix++){
        auto a=vertex(ix,iz,n),b=vertex(ix+1,iz,n);
        auto c=vertex(ix,iz+1,n),d=vertex(ix+1,iz+1,n);
        // Shared vertices guarantee exact heights at equal-resolution edges.
        render.indices.insert(render.indices.end(),{a,c,b,b,c,d});
    }
    // All LODs include skirts, covering T-junction differences at mixed LOD.
    // Collision meshes remain on exact continuous height samples.
    auto add_skirt=[&](std::vector<std::uint32_t> const& edge){
        const auto first=static_cast<std::uint32_t>(render.vertices.size());
        for(auto base:edge){
            auto v=render.vertices[base];
            v.position.y-=static_cast<float>(config_.skirt_depth_m);
            render.vertices.push_back(v);
        }
        for(std::uint32_t i=0;i+1<edge.size();i++){
            const auto a=edge[i],b=edge[i+1],c=first+i,d=first+i+1;
            render.indices.insert(render.indices.end(),{a,b,c,b,d,c});
        }
    };
    std::vector<std::uint32_t> edge;edge.reserve(n+1);
    for(std::uint32_t i=0;i<=n;i++)edge.push_back(vertex(i,0,n));
    add_skirt(edge);edge.clear();
    for(std::uint32_t i=0;i<=n;i++)edge.push_back(vertex(n,i,n));
    add_skirt(edge);edge.clear();
    for(std::uint32_t i=0;i<=n;i++)edge.push_back(vertex(n-i,n,n));
    add_skirt(edge);edge.clear();
    for(std::uint32_t i=0;i<=n;i++)edge.push_back(vertex(0,n-i,n));
    add_skirt(edge);
    auto const cn=config_.collision_quads;
    auto const ch=w/static_cast<double>(cn);
    auto& collision=chunk.collision;
    collision.vertices.reserve((cn+1)*(cn+1));
    collision.indices.reserve(6*cn*cn);
    for(std::uint32_t iz=0;iz<=cn;iz++)
        for(std::uint32_t ix=0;ix<=cn;ix++){
            double x=ox+ix*ch,z=oz+iz*ch;
            collision.vertices.push_back({
                static_cast<float>(x),static_cast<float>(height(x,z)),
                static_cast<float>(z)});
        }
    for(std::uint32_t iz=0;iz<cn;iz++)
        for(std::uint32_t ix=0;ix<cn;ix++){
            auto a=vertex(ix,iz,cn),b=vertex(ix+1,iz,cn);
            auto c=vertex(ix,iz+1,cn),d=vertex(ix+1,iz+1,cn);
            collision.indices.insert(collision.indices.end(),{a,c,b,b,c,d});
        }
    return chunk;
}
namespace {
std::int32_t stabilized_axis(double predicted,double width,
                             std::optional<std::int32_t> prior){
    auto const cast=[](double v){return static_cast<std::int32_t>(v);};
    if(!prior)return cast(std::floor(predicted/width));
    auto const n=static_cast<double>(*prior);
    if(predicted>=(n+.75)*width)
        return cast(std::floor(predicted/width+.25));
    if(predicted<(n-.75)*width)
        return cast(std::ceil(predicted/width-.25));
    return *prior;
}
}
TerrainStreamer::TerrainStreamer(TerrainConfig cfg):provider_(cfg){}
TerrainDelta TerrainStreamer::update(Vec3d camera,Vec3d velocity) {
    if(!finite(camera)||!finite(velocity)||
       std::abs(camera.x)>9e6||std::abs(camera.z)>9e6 ||
       std::abs(velocity.x)>10000||std::abs(velocity.z)>10000)
       throw std::invalid_argument("Invalid terrain streaming location");
    auto const& cfg=provider_.config();
    // A 0m root used to alternate between chunk -1 and 0 due to tiny
    // changes in speed/position, recooking 39 LOD meshes per frame.
    // A 0.75-chunk deadband prevents boundary LOD thrashing.
    auto const predicted_x=camera.x+
        std::clamp(velocity.x*.35,-cfg.width_m,cfg.width_m);
    auto const predicted_z=camera.z+
        std::clamp(velocity.z*.35,-cfg.width_m,cfg.width_m);
    auto const cx=stabilized_axis(predicted_x,cfg.width_m,
        focus_center_?std::optional<std::int32_t>(focus_center_->x):std::nullopt);
    auto const cz=stabilized_axis(predicted_z,cfg.width_m,
        focus_center_?std::optional<std::int32_t>(focus_center_->z):std::nullopt);
    focus_center_=ChunkKey{cx,cz};
    std::map<ChunkKey,std::uint32_t> wanted;
    for(int iz=-static_cast<int>(cfg.radius);iz<=static_cast<int>(cfg.radius);iz++)
    for(int ix=-static_cast<int>(cfg.radius);ix<=static_cast<int>(cfg.radius);ix++){
        const int ring=std::max(std::abs(ix),std::abs(iz));
        auto const lod=static_cast<std::uint32_t>(std::max(0,ring-1));
        wanted.emplace(ChunkKey{cx+ix,cz+iz},std::min(cfg.max_lod,lod));
    }
    TerrainDelta delta;
    for(auto const& [key,lod]:resident_){
        auto found=wanted.find(key);
        if(found==wanted.end()||found->second!=lod)
            delta.evicted.push_back(key);
    }
    // Generate the closest chunks first and bias the forward hemisphere.
    // This makes newly entered ground ready before distant peripheral LODs.
    std::vector<std::pair<ChunkKey,std::uint32_t>> missing;
    missing.reserve(wanted.size());
    for(auto const& [key,lod]:wanted){
        auto found=resident_.find(key);
        if(found==resident_.end()||found->second!=lod)
            missing.emplace_back(key,lod);
    }
    auto priority=[&](ChunkKey key){
        const double dx=key.x-cx,dz=key.z-cz;
        const double speed=std::hypot(velocity.x,velocity.z);
        const double ahead=speed>.01?
            (dx*velocity.x+dz*velocity.z)/speed:0.;
        const double ring=std::max(std::abs(dx),std::abs(dz));
        return ring*100.-ahead*7.;
    };
    std::sort(missing.begin(),missing.end(),[&](auto const& a,auto const& b){
        const double pa=priority(a.first),pb=priority(b.first);
        if(pa!=pb)return pa<pb;
        return a.first<b.first;
    });
    for(auto const& [key,lod]:missing)
        delta.uploaded.push_back(provider_.generate(key,lod));
    if(!delta.evicted.empty()||!delta.uploaded.empty())
        ++generation_;
    resident_.swap(wanted);
    delta.resident_count=resident_.size();
    delta.generation=generation_;
    return delta;
}
TerrainAsyncStreamer::TerrainAsyncStreamer(TerrainConfig config)
    :streamer_(config){}
TerrainAsyncStreamer::~TerrainAsyncStreamer() {
    // std::future joins an in-flight std::async task before we destroy the
    // provider/streamer, so no worker can touch freed project resources.
    if(task_.valid())task_.wait();
}
ChunkKey TerrainAsyncStreamer::center(Vec3d focus,Vec3d velocity) const {
    if(!finite(focus)||!finite(velocity)||
       std::abs(focus.x)>9e6||std::abs(focus.z)>9e6||
       std::abs(velocity.x)>10000||std::abs(velocity.z)>10000)
        throw std::invalid_argument("Invalid async terrain focus");
    auto const w=streamer_.terrain().config().width_m;
    auto prior_x=initialized_?std::optional<std::int32_t>(launched_center_.x):
        std::nullopt;
    auto prior_z=initialized_?std::optional<std::int32_t>(launched_center_.z):
        std::nullopt;
    return {
        stabilized_axis(focus.x+std::clamp(velocity.x*.35,-w,w),w,prior_x),
        stabilized_axis(focus.z+std::clamp(velocity.z*.35,-w,w),w,prior_z)};
}
std::optional<TerrainDelta> TerrainAsyncStreamer::update(Vec3d focus,Vec3d velocity) {
    auto const wanted=center(focus,velocity);
    // Initial chunk ring must exist before the first physics step.
    if(!initialized_){
        auto delta=streamer_.update(focus,velocity);
        launched_center_=wanted;
        initialized_=true;
        return delta;
    }
    std::optional<TerrainDelta> finished;
    if(task_.valid()){
        if(task_.wait_for(std::chrono::seconds(0))!=std::future_status::ready)
            return std::nullopt; // no frame wait, no CPU terrain generation
        finished=task_.get(); // propagate worker errors to owning thread
    }
    if(wanted==launched_center_)return finished;
    launched_center_=wanted;
    task_=std::async(std::launch::async,[this,focus,velocity] {
        return streamer_.update(focus,velocity);
    });
    return finished;
}
FloatingOrigin::FloatingOrigin(double threshold,double grid)
    :threshold_(threshold),grid_(grid) {
    if(!std::isfinite(threshold)||!std::isfinite(grid)||
       grid<1.||threshold<2.*grid||threshold>1e7)
        throw std::invalid_argument("Invalid Floating Origin settings");
}
Vec3d FloatingOrigin::to_local(Vec3d absolute) const noexcept {
    return minus(absolute,origin_);
}
Vec3d FloatingOrigin::to_absolute(Vec3d local) const noexcept {
    return plus(local,origin_);
}
std::optional<Vec3d> FloatingOrigin::proposal(Vec3d focus) const {
    if(!finite(focus)||std::abs(focus.x)>1e12||std::abs(focus.z)>1e12)
        throw std::invalid_argument("Invalid Floating Origin focus");
    auto const local=to_local(focus);
    if(std::max(std::abs(local.x),std::abs(local.z))<threshold_)
        return std::nullopt;
    return Vec3d{
        std::floor(focus.x/grid_)*grid_,0.,
        std::floor(focus.z/grid_)*grid_};
}
void FloatingOrigin::commit(Vec3d next) {
    if(!finite(next)||std::abs(next.x)>1e12||std::abs(next.z)>1e12)
        throw std::invalid_argument("Invalid Floating Origin target");
    origin_={next.x,0.,next.z};
}
CityRoot::CityRoot(std::uint64_t stable_id):id_(stable_id){
    if(!id_)throw std::invalid_argument("CityRoot requires a stable ID");
}
void CityRoot::set_motion(Vec3d p,double yaw,Vec3d v,double rate){
    if(!finite(p)||!finite(v)||!std::isfinite(yaw)||!std::isfinite(rate)||
       std::abs(rate)>100.||std::max({std::abs(p.x),std::abs(p.y),
           std::abs(p.z)})>1e9)throw std::invalid_argument("Invalid CityRoot pose");
    position_=p;velocity_=v;yaw_=yaw;yaw_rate_=rate;
}
void CityRoot::advance(double dt){
    if(!std::isfinite(dt)||dt<=0.||dt>1./30.)
        throw std::invalid_argument("CityRoot tick must be fixed and bounded");
    position_=plus(position_,{velocity_.x*dt,velocity_.y*dt,velocity_.z*dt});
    yaw_=std::remainder(yaw_+yaw_rate_*dt,2*pi);
}
Vec3d CityRoot::rotate_to_world(Vec3d p) const noexcept{
    const double c=std::cos(yaw_),s=std::sin(yaw_);
    return {c*p.x+s*p.z,p.y,-s*p.x+c*p.z};
}
Vec3d CityRoot::rotate_to_local(Vec3d p) const noexcept{
    const double c=std::cos(yaw_),s=std::sin(yaw_);
    return {c*p.x-s*p.z,p.y,s*p.x+c*p.z};
}
Vec3d CityRoot::to_world(Vec3d p) const noexcept{
    return plus(position_,rotate_to_world(p));
}
Vec3d CityRoot::to_local(Vec3d p) const noexcept{
    return rotate_to_local(minus(p,position_));
}
Vec3d CityRoot::support_velocity(Vec3d point_world) const noexcept{
    const auto r=minus(point_world,position_);
    // Angular velocity w=(0,yaw_rate,0) => w x r=(w*r.z,0,-w*r.x).
    return plus(velocity_,{yaw_rate_*r.z,0.,-yaw_rate_*r.x});
}
ActorState CityRoot::transfer_to(CityRoot const& other,ActorState a) const noexcept{
    auto point=to_world(a.position);
    auto absolute_vel=plus(support_velocity(point),rotate_to_world(a.velocity));
    auto relative=minus(absolute_vel,other.support_velocity(point));
    return {other.to_local(point),other.rotate_to_local(relative)};
}
CityGpuTransformBatch::CityGpuTransformBatch(std::uint64_t id):root_id_(id){
    if(!id)throw std::invalid_argument("Invalid GPU batch root ID");
}
void CityGpuTransformBatch::set_instances(std::vector<GpuInstance> members){
    if(members.size()>16384)throw std::length_error("GPU city batch exceeds budget");
    std::set<std::uint64_t> ids;
    for(auto const& member:members){
        if(!member.stable_id || !finite(member.local_position) ||
           !std::isfinite(member.local_yaw) ||
           !ids.emplace(member.stable_id).second)
            throw std::invalid_argument("Duplicate or invalid City GPU instance");
    }
    members_.swap(members);
    previous_.clear();
}
std::vector<GpuTransform> CityGpuTransformBatch::collect_dirty(CityRoot const& root){
    if(root.id()!=root_id_)throw std::invalid_argument("Wrong CityRoot batch owner");
    std::vector<GpuTransform> current;
    current.reserve(members_.size());
    for(auto const& item:members_){
        GpuTransform out;
        out.stable_id=item.stable_id;
        out.world=matrix(root.yaw()+item.local_yaw,
                         root.to_world(item.local_position));
        current.push_back(out);
    }
    std::vector<GpuTransform> dirty;
    dirty.reserve(current.size());
    for(std::size_t i=0;i<current.size();i++)
        if(i>=previous_.size() || current[i].stable_id!=previous_[i].stable_id ||
           current[i].world!=previous_[i].world)
            dirty.push_back(current[i]);
    previous_.swap(current);
    return dirty;
}
} // namespace northstarcpp::world
