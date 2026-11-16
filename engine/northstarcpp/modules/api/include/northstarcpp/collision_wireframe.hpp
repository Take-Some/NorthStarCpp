// Editor-only wire segments for authored collision shapes.
// Shared by GPU and Qt proxy paths; never changes the render-provider ABI.
#pragma once
#include <northstarcpp/collision_authoring.hpp>
#include <northstarcpp/collision_mesh.hpp>
#include <northstarcpp/collision_hulls.hpp>
#include <unordered_set>
#include <array>
#include <cmath>

namespace northstarcpp::collision_wireframe {
template<class Emit>
bool draw_cooked(Scene const& scene,SceneObject const& actor,Emit&& send_line,
                 int selected_hull=-1) {
    auto settings=collision_authoring::for_object(scene,actor);
    if(settings.shape!=collision_authoring::Shape::AutoConvex)return false;
    auto baked=collision_hulls::lookup(scene,actor.node.id);
    if(!baked||baked->source!=actor.node.asset||
       baked->splits_num!=settings.splits_num)return false;
    for(std::size_t h=0;h<baked->hulls.size();++h) {
        if(selected_hull>=0&&std::size_t(selected_hull)!=h)continue;
        auto const& hull=baked->hulls[h];
        auto transformed=collision_hulls::transformed(hull,actor,settings,false);
        std::unordered_set<std::uint64_t> drawn;
        std::size_t count=0;
        for(std::size_t n=0;n+2<hull.indices.size();n+=3) {
            for(int e=0;e<3;++e) {
                auto a=hull.indices[n+e],b=hull.indices[n+(e+1)%3];
                auto const key=(std::uint64_t(std::min(a,b))<<32)|std::max(a,b);
                if(!drawn.insert(key).second||count++>=3600)continue;
                Vector3 start{
                    actor.position.x+transformed[3*a],
                    actor.position.y+transformed[3*a+1],
                    actor.position.z+transformed[3*a+2]};
                Vector3 end{
                    actor.position.x+transformed[3*b],
                    actor.position.y+transformed[3*b+1],
                    actor.position.z+transformed[3*b+2]};
                send_line(start,end);
            }
        }
    }
    return true;
}
template<class Emit>
void draw(Scene const& scene,SceneObject const& actor,Emit&& send_line,
          bool detailed_mesh=false,int selected_hull=-1) {
    if(detailed_mesh&&draw_cooked(scene,actor,send_line,selected_hull))return;
    auto const config=collision_authoring::for_object(scene,actor);
    auto const& dim=config.dimensions;
    // Source physics ABI uses bottom-anchored objects and unscaled collider
    // dimensions. Keep the diagnostic wire geometry identical to simulation.
    Vector3 const base{
        actor.position.x+config.offset.x,
        actor.position.y+config.offset.y,
        actor.position.z+config.offset.z};
    auto const line=[&](Vector3 a,Vector3 b){send_line(a,b);};
    if(config.shape==collision_authoring::Shape::ComplexMesh ||
       config.shape==collision_authoring::Shape::AutoConvex) {
        // Avoid decoding and rendering thousands of triangles for 80+ actors.
        // Detailed mesh wireframe is reserved for the selected object.
        if(detailed_mesh) {
            if(auto m=collision_mesh::lookup(scene,actor.node.id)) {
                auto xyz=collision_mesh::transform(*m,actor,config,false);
                std::size_t segments=0;
                auto world=[&](std::uint32_t i)->Vector3 {
                    return {base.x-config.offset.x+xyz[3*i],
                            base.y-config.offset.y+xyz[3*i+1],
                            base.z-config.offset.z+xyz[3*i+2]};
                };
                for(std::size_t i=0;i+2<m->indices.size()&&segments<3600;i+=3) {
                    auto const a=world(m->indices[i]),b=world(m->indices[i+1]),
                               c=world(m->indices[i+2]);
                    line(a,b);line(b,c);line(c,a);
                    segments+=3;
                }
                return;
            }
        }
        if(!detailed_mesh)return;
        // Missing cooked geometry: show the proxy bounding box, not a fake hull.
    }
    if(config.shape==collision_authoring::Shape::Box ||
       config.shape==collision_authoring::Shape::AutoConvex ||
       config.shape==collision_authoring::Shape::ComplexMesh) {
        Vector3 vertices[8];
        for(int k=0;k<8;++k)
            vertices[k]={base.x+((k&1)?.5f:-.5f)*dim.x,
                         base.y+((k&2)?dim.y:0.f),
                         base.z+((k&4)?.5f:-.5f)*dim.z};
        for(int k=0;k<8;++k)
            for(int axis:{1,2,4})
                if(!(k&axis))line(vertices[k],vertices[k|axis]);
        return;
    }
    constexpr int steps=24;
    constexpr float tau=6.2831853071795864769f;
    float const r=dim.x*.5f;
    auto const ring=[&](float y,float radius) {
        for(int i=0;i<steps;++i) {
            float const a=tau*i/steps,b=tau*(i+1)/steps;
            line({base.x+radius*std::cos(a),y,base.z+radius*std::sin(a)},
                 {base.x+radius*std::cos(b),y,base.z+radius*std::sin(b)});
        }
    };
    if(config.shape==collision_authoring::Shape::Sphere) {
        Vector3 center{base.x,base.y+r,base.z};
        ring(center.y,r);
        for(int i=0;i<steps;++i) {
            float const a=tau*i/steps,b=tau*(i+1)/steps;
            line({center.x+r*std::cos(a),center.y+r*std::sin(a),center.z},
                 {center.x+r*std::cos(b),center.y+r*std::sin(b),center.z});
            line({center.x,center.y+r*std::sin(a),center.z+r*std::cos(a)},
                 {center.x,center.y+r*std::sin(b),center.z+r*std::cos(b)});
        }
        return;
    }
    // Capsule: two hemisphere centers separated by a cylinder along Y.
    float const lower=base.y+r,upper=base.y+dim.y-r;
    ring(lower,r);ring(upper,r);
    for(int i=0;i<steps;i+=6) {
        float const a=tau*i/steps;
        line({base.x+r*std::cos(a),lower,base.z+r*std::sin(a)},
             {base.x+r*std::cos(a),upper,base.z+r*std::sin(a)});
    }
    constexpr int arcs=12;
    for(int k=0;k<4;++k) {
        float const angle=tau*k/4;
        for(int i=0;i<arcs;++i) {
            float const a=(1.570796327f*i)/arcs;
            float const b=(1.570796327f*(i+1))/arcs;
            for(int sign:{-1,1}) {
                float const center=sign>0?upper:lower;
                auto point=[&](float t)->Vector3 {
                    return {base.x+r*std::cos(t)*std::cos(angle),
                            center+sign*r*std::sin(t),
                            base.z+r*std::cos(t)*std::sin(angle)};
                };
                line(point(a),point(b));
            }
        }
    }
}
} // namespace northstarcpp::collision_wireframe
