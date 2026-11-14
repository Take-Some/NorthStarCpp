// NorthStarCpp engine.perlin ABI and SDK regression tests.
#include <northstarcpp/perlin.hpp>
#include <northstarcpp/perlin_plugin.h>
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {
void check(bool condition,char const* error) {
    if (!condition) throw std::runtime_error(error);
}
bool approximately_equal(float a,float b,float tolerance=0.00001f) {
    return std::abs(a-b)<=tolerance;
}
void validate_provider(char const* filename) {
    HMODULE module=LoadLibraryA(filename);
    check(module!=nullptr,"Cannot load Perlin provider");
    auto query=reinterpret_cast<NorthStarModuleQuery>(
        GetProcAddress(module,"northstar_module_query"));
    check(query!=nullptr,"Perlin provider does not export query");
    auto api=query(NORTHSTARCPP_MODULE_ABI,NORTHSTARCPP_CAPABILITY_PERLIN,
                   NORTHSTARCPP_PERLIN_ABI);
    check(api!=nullptr && api->api_size>=sizeof(NorthStarPerlinApi) &&
        std::string(api->capability)=="engine.perlin","Perlin provider contract mismatch");
    check(query(NORTHSTARCPP_MODULE_ABI+1,NORTHSTARCPP_CAPABILITY_PERLIN,
                NORTHSTARCPP_PERLIN_ABI)==nullptr,"Invalid host ABI was accepted");
    check(query(NORTHSTARCPP_MODULE_ABI,NORTHSTARCPP_CAPABILITY_PERLIN,
                NORTHSTARCPP_PERLIN_ABI+1)==nullptr,"Invalid Perlin ABI was accepted");
    auto const& callbacks=*static_cast<NorthStarPerlinApi const*>(api->api);
    check(callbacks.create&&callbacks.destroy&&callbacks.sample1&&callbacks.sample2&&callbacks.sample3&&
        callbacks.fractal1&&callbacks.fractal2&&callbacks.fractal3&&callbacks.fill2,"Incomplete Perlin provider");
    NorthStarPerlinConfig bad{sizeof(NorthStarPerlinConfig),257,0,0,5};
    check(callbacks.create(&bad)==nullptr,"Invalid Perlin period accepted");
    bad.period_x=0;
    void* context=callbacks.create(&bad);
    check(context!=nullptr,"Perlin C ABI cannot create context");
    float out=123;
    check(!callbacks.sample2(context,std::numeric_limits<double>::quiet_NaN(),0,&out)
          && out==123,"Invalid sample did not fail without modifying output");
    check(!callbacks.sample1(context,0,nullptr),"Null 1D output accepted");
    check(!callbacks.sample2(context,0,0,nullptr),"Null output accepted");
    NorthStarPerlinFractal invalid{sizeof(NorthStarPerlinFractal),0,
        NORTHSTAR_PERLIN_FBM,0,1,2,0.5};
    check(!callbacks.fractal2(context,1,1,&invalid,&out) && out==123,
          "Invalid octaves accepted");
    invalid.octaves=1;
    invalid.frequency=std::numeric_limits<double>::infinity();
    check(!callbacks.fractal2(context,1,1,&invalid,&out) && out==123,
          "Invalid frequency accepted");
    callbacks.destroy(context);
    FreeLibrary(module);
}
void run_default(char const* filename) {
    validate_provider(filename);
    northstarcpp::PerlinOptions options;
    options.seed=0xDEADBEEFull;
    options.period_x=17;
    options.period_y=31;
    options.period_z=13;
    northstarcpp::PerlinNoise n(options),same(options);
    check(n.provider_name()=="Improved Perlin","Wrong Perlin provider");
    check(n.provider_version()=="1.0.0","Wrong Perlin version");
    check(approximately_equal(n.sample1(10),0.f) &&
          approximately_equal(n.sample2(10,23),0.f) && approximately_equal(n.sample3(11,9,6),0.f),
          "Noise must vanish at integer lattice nodes");
    for (int i=0;i<100;++i) {
        double x=i*0.157-3.3,y=i*0.273+0.07,z=i*0.113-2.5;
        float one=n.sample1(x),a=n.sample2(x,y),b=n.sample3(x,y,z);
        check(std::isfinite(one)&&std::abs(one)<=1.f&&
              std::isfinite(a)&&std::isfinite(b)&&
              std::abs(a)<=1.f && std::abs(b)<=1.f,
              "Perlin sample out of normalized range");
        check(one==same.sample1(x)&&a==same.sample2(x,y)&&b==same.sample3(x,y,z),
              "Same seed must reproduce exact samples");
        check(approximately_equal(one,n.sample1(x+17)),"1D period mismatch");
        check(approximately_equal(a,n.sample2(x+17,y))&&approximately_equal(a,n.sample2(x,y+31)),
              "2D period mismatch");
        check(approximately_equal(b,n.sample3(x+17,y,z))&&
              approximately_equal(b,n.sample3(x,y+31,z))&&approximately_equal(b,n.sample3(x,y,z+13)),
              "3D period mismatch");
    }
    auto changed=options;
    changed.seed++;
    northstarcpp::PerlinNoise other(changed);
    bool different=false;
    for(int i=0;i<24;++i)
        different |= n.sample3(i*0.19,i*0.17,0.47)!=
                     other.sample3(i*0.19,i*0.17,0.47);
    check(different,"Different seeds produce identical noise");
    northstarcpp::PerlinFractal fractal;
    for (auto mode : {northstarcpp::PerlinMode::fbm,
                      northstarcpp::PerlinMode::turbulence,
                      northstarcpp::PerlinMode::ridged}) {
        fractal.mode=mode;
        for(int i=0;i<20;++i) {
            float one=n.fractal1(i*0.087,fractal);
            float a=n.fractal2(i*0.087,i*0.23,fractal);
            float b=n.fractal3(i*0.087,i*0.23,0.129,fractal);
            check(std::isfinite(one)&&one>=-1&&one<=1&&
                  std::isfinite(a)&&std::isfinite(b)&&a>=-1&&a<=1&&
                  b>=-1&&b<=1,"Fractal out of range");
            if(mode!=northstarcpp::PerlinMode::fbm)
                check(one>=0&&a>=0&&b>=0,"Unsigned fractal mode was negative");
        }
    }
    fractal.mode=northstarcpp::PerlinMode::fbm;
    fractal.octaves=1;
    fractal.frequency=1;
    check(approximately_equal(n.sample1(0.22),n.fractal1(0.22,fractal)),
          "Single-octave 1D FBM differs from raw noise");
    check(approximately_equal(n.sample2(0.22,0.57),n.fractal2(0.22,0.57,fractal)),
          "Single-octave FBM differs from raw noise");
    fractal.octaves=5;
    std::uint32_t const width=128,height=64,stride=139;
    auto packed=n.fill2(-2.5,1.25,0.07,0.06,width,height,fractal);
    std::vector<float> strided(static_cast<std::size_t>(stride)*height,12345.f);
    n.fill2(-2.5,1.25,0.07,0.06,width,height,stride,fractal,strided.data());
    for(std::uint32_t row=0;row<height;++row) {
        for(std::uint32_t col=0;col<width;++col)
            check(packed[row*width+col]==strided[row*stride+col],
                  "Packed and strided grids differ");
        for(std::uint32_t col=width;col<stride;++col)
            check(strided[row*stride+col]==12345.f,
                  "Grid writer overwrote padding");
    }
    bool rejected=false;
    try {
        n.fill2(0,0,1,1,0,10,fractal);
    } catch (std::invalid_argument const&) {rejected=true;}
    check(rejected,"Invalid C++ grid dimensions accepted");
    rejected=false;
    try {
        auto bad=fractal;
        bad.octaves=0;
        n.fractal2(0,0,bad);
    } catch(std::runtime_error const&) {rejected=true;}
    check(rejected,"Invalid C++ fractal configuration accepted");
    // Read-only sample operations are independently thread-safe.
    std::atomic<bool> threads_ok{true};
    std::vector<std::thread> workers;
    for(int i=0;i<6;++i)
        workers.emplace_back([&] {
            for(int j=0;j<3000;++j) {
                double x=j*0.0163,y=j*0.0059;
                if(n.sample2(x,y)!=same.sample2(x,y) ||
                   n.sample3(x,y,0.33)!=same.sample3(x,y,0.33))
                    threads_ok=false;
            }
        });
    for(auto& thread:workers)thread.join();
    check(threads_ok,"Concurrent Perlin sampling was non-deterministic");
    std::cout<<"engine.perlin ABI, 1D/2D/3D, seed, periods, fractals, grids, threads: PASS\n";
}
} // namespace
int main(int argc,char** argv) {
    if(argc<2 || argc>3)return 2;
    try {
        std::string mode=argv[1];
        if(mode=="default" && argc==3) run_default(argv[2]);
        else if(mode=="reject" && argc==2) {
            try {northstarcpp::PerlinNoise n; }
            catch(std::runtime_error const& e) {
                if(std::string(e.what()).find("engine.perlin")!=std::string::npos)
                    return 0;
            }
            return 3;
        } else return 2;
        return 0;
    } catch(std::exception const& e) {
        std::cerr<<e.what()<<'\n';
        return 1;
    }
}
