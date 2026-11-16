// V-HACD 4.1 (BSD-3-Clause); single translation unit for implementation.
#define ENABLE_VHACD_IMPLEMENTATION 1
#define VHACD_DISABLE_THREADING 1
#include <VHACD.h>
#include <northstarcpp/collision_cooker.hpp>
#include <algorithm>
#include <cmath>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

namespace northstarcpp::collision_cooker {
std::vector<Hull> cook(std::vector<float> const& xyz,
                       std::vector<std::uint32_t> const& triangles,
                       Options const& opts) {
    if(xyz.size()<12 || xyz.size()%3 || xyz.size()>54000 ||
       triangles.size()<12 || triangles.size()%3 || triangles.size()>54000 ||
       opts.max_hulls<1 || opts.max_hulls>16 ||
       opts.resolution<10000 || opts.resolution>1000000 ||
       opts.max_vertices<4 || opts.max_vertices>128 ||
       !std::isfinite(opts.volume_error_percent) ||
       opts.volume_error_percent<0.01 || opts.volume_error_percent>10.)
        throw std::invalid_argument("Invalid V-HACD geometry or settings");
    for(float f:xyz)
        if(!std::isfinite(f)||std::abs(f)>100000.f)
            throw std::invalid_argument("V-HACD contains invalid coordinates");
    for(auto i:triangles)
        if(i>=xyz.size()/3)
            throw std::invalid_argument("V-HACD triangle outside vertex range");
    using Handle=std::unique_ptr<VHACD::IVHACD,void(*)(VHACD::IVHACD*)>;
    Handle solver(VHACD::CreateVHACD(),[](VHACD::IVHACD* x){if(x)x->Release();});
    if(!solver)throw std::runtime_error("V-HACD allocation failed");
    struct Callback final : VHACD::IVHACD::IUserCallback {
        std::atomic<int>* progress{};
        std::atomic<bool>* cancel{};
        VHACD::IVHACD* solver{};
        Callback(std::atomic<int>* p,std::atomic<bool>* c,VHACD::IVHACD* s)
            :progress(p),cancel(c),solver(s){}
        void Update(double overall,double,const char*,const char*) override {
            if(progress)
                progress->store(std::clamp(int(overall),0,99),
                                std::memory_order_relaxed);
            if(cancel && cancel->load(std::memory_order_relaxed))
                solver->Cancel();
        }
    };
    if(opts.cancel_requested && opts.cancel_requested->load())
        throw std::runtime_error("V-HACD cancellation requested");
    Callback callback(opts.progress_percent,opts.cancel_requested,solver.get());
    VHACD::IVHACD::Parameters settings;
    settings.m_callback=&callback;
    settings.m_maxConvexHulls=opts.max_hulls;
    settings.m_resolution=opts.resolution;
    settings.m_maxNumVerticesPerCH=opts.max_vertices;
    settings.m_minimumVolumePercentErrorAllowed=opts.volume_error_percent;
    settings.m_asyncACD=false;
    settings.m_shrinkWrap=true;
    if(!solver->Compute(xyz.data(),std::uint32_t(xyz.size()/3),
        triangles.data(),std::uint32_t(triangles.size()/3),settings))
        throw std::runtime_error("V-HACD failed; check that mesh is closed and nondegenerate");
    std::vector<Hull> result;
    if(opts.cancel_requested && opts.cancel_requested->load())
        throw std::runtime_error("V-HACD cancelled");
    auto const count=solver->GetNConvexHulls();
    if(count==0||count>opts.max_hulls)
        throw std::runtime_error("V-HACD returned no usable hulls");
    result.reserve(count);
    for(std::uint32_t i=0;i<count;++i) {
        VHACD::IVHACD::ConvexHull h;
        if(!solver->GetConvexHull(i,h) || h.m_points.size()<4 ||
           h.m_points.size()>128 || h.m_triangles.size()<4)
            throw std::runtime_error("V-HACD returned degenerate hull");
        Hull baked;
        baked.xyz.reserve(3*h.m_points.size());
        baked.indices.reserve(3*h.m_triangles.size());
        for(auto const& v:h.m_points) {
            for(double d:{v.mX,v.mY,v.mZ}) {
                if(!std::isfinite(d) || std::abs(d)>100000.)
                    throw std::runtime_error("V-HACD hull has nonfinite vertex");
                baked.xyz.push_back(static_cast<float>(d));
            }
        }
        for(auto const& f:h.m_triangles) {
            for(auto v:{f.mI0,f.mI1,f.mI2}) {
                if(v>=h.m_points.size())
                    throw std::runtime_error("V-HACD hull index invalid");
                baked.indices.push_back(v);
            }
        }
        result.push_back(std::move(baked));
    }
    if(opts.progress_percent)opts.progress_percent->store(100);
    return result;
}
} // namespace northstarcpp::collision_cooker
