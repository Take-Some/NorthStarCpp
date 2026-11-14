#include <northstarcpp/perlin_service.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
using boost::property_tree::ptree;
static ptree parse(std::string const& value) {
    ptree object;std::istringstream stream(value);
    boost::property_tree::read_json(stream,object);
    return object;
}
static void check(bool okay,char const* why) {
    if(!okay)throw std::runtime_error(why);
}
int main() {
    try {
        using northstarcpp::call_perlin_service_json;
        auto call=[](char const* method,char const* payload) {
            auto response=parse(call_perlin_service_json("engine.perlin",method,payload));
            check(response.get<bool>("ok"),"Perlin JSON host call failed");
            return parse(response.get<std::string>("payload_json"));
        };
        auto s=call("perlin.sample_json_v1",
            R"({"seed":2026,"point":[1.25,-3.75],"normalized_01":false})");
        auto s2=call("perlin.sample_json_v1",
            R"({"seed":2026,"point":[1.25,-3.75],"normalized_01":false})");
        check(s.get<double>("value")==s2.get<double>("value"),
              "JSON sample is not deterministic");
        auto normalized=call("perlin.sample_json_v1",
            R"({"seed":2026,"point":[1.25,-3.75],"normalized_01":true})");
        check(normalized.get<double>("value")>=0 &&
              normalized.get<double>("value")<=1,"Normalized JSON sample invalid");
        auto fbm=call("perlin.fbm_json_v1",
            R"({"seed":1,"point":[2.5],"octaves":3,"frequency":1,"lacunarity":2,"gain":0.5})");
        check(fbm.get<double>("value")>=-1 && fbm.get<double>("value")<=1,
              "1D fBm JSON response out of range");
        auto batch=call("perlin.batch_json_v1",
            R"({"seed":12,"points":[[2],[1,3],[1,3,5]],"fbm":{"octaves":4,"frequency":1,"lacunarity":2,"gain":0.5},"normalized_01":true})");
        auto values=batch.get_child("values");
        check(values.size()==3,"Batch JSON response has wrong length");
        for(auto const& v:values) {
            float n=v.second.get_value<float>();
            check(n>=0.f && n<=1.f,"Batch JSON normalized value outside [0,1]");
        }
        auto failure=parse(call_perlin_service_json("engine.perlin",
            "perlin.sample_json_v1",R"({"seed":0,"point":[1,2,3,4]})"));
        check(!failure.get<bool>("ok"),"Invalid 4D point must be rejected");
        failure=parse(call_perlin_service_json("engine.noise",
            "perlin.sample_json_v1",R"({"seed":0,"point":[1]})"));
        check(!failure.get<bool>("ok"),"Deprecated service should not alias silently");
        failure=parse(call_perlin_service_json("engine.perlin",
            "perlin.fbm_json_v1",R"({"seed":1,"point":[0],"octaves":17})"));
        check(!failure.get<bool>("ok"),"Invalid octave count was accepted");
        std::cout<<"Native engine.perlin JSON sample/fBm/batch and rejection: PASS\n";
        return 0;
    } catch(std::exception const& e) {
        std::cerr<<e.what()<<'\n';return 1;
    }
}
