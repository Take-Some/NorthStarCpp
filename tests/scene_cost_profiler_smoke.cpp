#include <northstarcpp/scene_cost_profiler.hpp>
#include <stdexcept>
#include <sstream>
using northstarcpp::SceneCostProfiler;
void check(bool v,char const* m){if(!v)throw std::runtime_error(m);}
int main() {
    SceneCostProfiler profiler(1024);
    for(unsigned i=1;i<=1000;++i) {
        auto const seq=profiler.record_cpu(i);
        check(seq==i,"bad sequence");
        if((i%2)==0)check(profiler.record_gpu(seq,i*2,8192+i),
                           "failed to correlate GPU sample");
    }
    auto const result=profiler.summary();
    check(result.cpu_us.samples==1000 && result.cpu_us.p50==500 &&
          result.cpu_us.p95==950 && result.cpu_us.p99==990 &&
          result.cpu_us.max==1000,"CPU percentiles incorrect");
    check(result.gpu_us.samples==500 && result.gpu_us.p95==1900 &&
          result.gpu_bytes.samples==500,"GPU percentiles or sample count incorrect");
    std::ostringstream csv;
    profiler.write_csv(csv);
    auto const text=csv.str();
    check(text.find("frame,cpu_us,gpu_us,gpu_bytes\n1,1,,\n2,2,4,8194\n")==0,
          "CSV sequence/timestamp alignment failed");
    SceneCostProfiler ring(4);
    for(unsigned i=1;i<=6;++i)ring.record_cpu(i);
    check(!ring.record_gpu(1,3),"expired GPU frame was accepted");
    check(ring.record_gpu(6,8),"latest GPU frame was rejected");
    auto const last=ring.summary();
    check(last.cpu_us.samples==4 && last.cpu_us.p50==4 &&
          last.cpu_us.p95==6 && last.gpu_us.samples==1,
          "rolling window percentile regression");
    return 0;
}
