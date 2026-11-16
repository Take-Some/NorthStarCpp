// CPU wall-time submission quantiles. These are not hardware GPU timestamps.
#include "viewport_frame_profile.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace northstarcpp::editor;
namespace {
void check(bool value,char const* reason) {
    if(!value)throw std::runtime_error(reason);
}
bool close(double a,double b) {return std::abs(a-b)<1.0e-8;}
}
int main() {
    try {
        ViewportFrameProfile profile;
        check(profile.count()==0 && profile.summarize().samples==0,
              "Empty telemetry should have no samples");
        profile.sample(-1.0);
        profile.sample(std::numeric_limits<double>::infinity());
        profile.sample(std::numeric_limits<double>::quiet_NaN());
        check(profile.count()==0,"Invalid frame times must be excluded");
        for(int i=1;i<=1000;++i)profile.sample(double(i));
        auto q=profile.summarize();
        check(q.samples==1000 && close(q.p50_ms,500.5) &&
              close(q.p95_ms,950.05) && close(q.p99_ms,990.01),
              "Quantiles disagree with sorted interpolation");
        for(int i=1001;i<=1030;++i)profile.sample(double(i));
        q=profile.summarize();
        check(q.samples==ViewportFrameProfile::capacity &&
              close(q.p50_ms,518.5),
              "Bounded rolling window did not evict oldest frames");
        profile.clear();
        check(profile.count()==0 && profile.summarize().samples==0,
              "Telemetry reset failed");
        std::cout<<"PASS: 1000-sample p50/p95/p99 and 1024-frame rolling window\n";
        return 0;
    }catch(std::exception const& error) {
        std::cerr<<"Viewport frame profile: "<<error.what()<<'\n';
        return 1;
    }
}
