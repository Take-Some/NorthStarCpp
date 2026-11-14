// Native Perlin JSON service for the YSC callServiceJson contract.
#include <northstarcpp/perlin_service.hpp>
#include <northstarcpp/perlin.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace northstarcpp {
namespace {
using Tree=boost::property_tree::ptree;
std::string escape_json(std::string_view raw) {
    static constexpr char hex[]="0123456789abcdef";
    std::string out;
    out.reserve(raw.size()+16);
    out.push_back('"');
    for(unsigned char c:raw) {
        switch(c) {
        case '"': out+="\\\""; break;
        case '\\':out+="\\\\"; break;
        case '\n':out+="\\n"; break;
        case '\r':out+="\\r"; break;
        case '\t':out+="\\t"; break;
        default:
            if(c<32) {
                out+="\\u00";out.push_back(hex[c>>4]);out.push_back(hex[c&15]);
            } else out.push_back(static_cast<char>(c));
        }
    }
    out.push_back('"');
    return out;
}
bool boolean(Tree const& node,char const* field,bool fallback=false) {
    auto opt=node.get_optional<std::string>(field);
    if(!opt)return fallback;
    if(*opt=="true")return true;
    if(*opt=="false")return false;
    throw std::invalid_argument(std::string("Expected boolean ")+field);
}
std::uint64_t seed(Tree const& node) {
    std::string value=node.get<std::string>("seed","0");
    if(value.empty() || value.find_first_not_of("0123456789")!=std::string::npos)
        throw std::invalid_argument("Perlin seed must be an unsigned integer");
    auto seed=std::stoull(value);
    if(seed>9007199254740991ull)
        throw std::invalid_argument("Perlin JSON seed exceeds JS safe integer");
    return seed;
}
double finite(std::string const& value) {
    std::size_t consumed=0;
    double result=std::stod(value,&consumed);
    if(consumed!=value.size() || !std::isfinite(result))
        throw std::invalid_argument("Perlin JSON coordinate is not finite");
    return result;
}
std::vector<double> point(Tree const& node) {
    std::vector<double> out;
    for(auto const& item:node) {
        if(!item.first.empty())
            throw std::invalid_argument("Perlin point must be a coordinate array");
        out.push_back(finite(item.second.get_value<std::string>()));
        if(out.size()>3)
            throw std::invalid_argument("Perlin point dimensions exceed 3");
    }
    if(out.empty())throw std::invalid_argument("Perlin point must have 1..3 coordinates");
    return out;
}
PerlinFractal fractal_config(Tree const& node) {
    PerlinFractal f;
    auto count=node.get<int>("octaves",4);
    if(count<1 || count>16)throw std::invalid_argument("Perlin octaves must be 1..16");
    f.octaves=static_cast<std::uint32_t>(count);
    f.frequency=node.get<double>("frequency",1.0);
    f.lacunarity=node.get<double>("lacunarity",2.0);
    f.persistence=node.get<double>("gain",0.5);
    if(!std::isfinite(f.frequency) || !std::isfinite(f.lacunarity) ||
       !std::isfinite(f.persistence) || f.frequency<=0 || f.lacunarity<1 ||
       f.persistence<0 || f.persistence>1)
        throw std::invalid_argument("Invalid Perlin fBm settings");
    return f;
}
float sample(PerlinNoise const& n,std::vector<double> const& p,
             PerlinFractal const* f) {
    if(p.size()==1)return f?n.fractal1(p[0],*f):n.sample1(p[0]);
    if(p.size()==2)return f?n.fractal2(p[0],p[1],*f):n.sample2(p[0],p[1]);
    return f?n.fractal3(p[0],p[1],p[2],*f):n.sample3(p[0],p[1],p[2]);
}
std::string number(float value,bool normalized) {
    double out=normalized ? std::clamp((static_cast<double>(value)+1.0)*0.5,0.0,1.0)
                          :static_cast<double>(value);
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream<<std::setprecision(9)<<out;
    return stream.str();
}
std::string dispatch(std::string_view method,Tree const& request) {
    PerlinNoise noise({seed(request),256,256,256});
    auto normalized=boolean(request,"normalized_01");
    if(method=="perlin.sample_json_v1" || method=="perlin.fbm_json_v1") {
        auto pt=request.get_child_optional("point");
        if(!pt)throw std::invalid_argument("Missing Perlin point");
        auto p=point(*pt);
        if(method=="perlin.fbm_json_v1") {
            auto options=fractal_config(request);
            return "{\"value\":"+number(sample(noise,p,&options),normalized)+"}";
        }
        return "{\"value\":"+number(sample(noise,p,nullptr),normalized)+"}";
    }
    if(method=="perlin.batch_json_v1") {
        auto points=request.get_child_optional("points");
        if(!points)throw std::invalid_argument("Missing Perlin batch points");
        auto opt=request.get_child_optional("fbm");
        bool use_fbm=opt && opt->data()!="null";
        PerlinFractal f;
        if(use_fbm) f=fractal_config(*opt);
        std::string result="{\"values\":[";
        std::size_t index=0;
        for(auto const& item:*points) {
            if(!item.first.empty())throw std::invalid_argument("Expected points array");
            if(index>=4096)throw std::invalid_argument("Perlin batch exceeds 4096 points");
            if(index++)result+=',';
            result+=number(sample(noise,point(item.second),use_fbm?&f:nullptr),normalized);
        }
        return result+"]}";
    }
    throw std::invalid_argument("Unsupported engine.perlin method");
}
} // namespace
std::string call_perlin_service_json(std::string_view service,
    std::string_view method,std::string_view request_json) {
    try {
        if(service!="engine.perlin")throw std::invalid_argument("Unsupported service");
        if(request_json.size()>262144u)throw std::invalid_argument("Perlin JSON request too large");
        std::istringstream stream{std::string(request_json)};
        Tree payload;
        boost::property_tree::read_json(stream,payload);
        std::string result=dispatch(method,payload);
        return "{\"ok\":true,\"payload_json\":"+escape_json(result)+"}";
    } catch(std::exception const& error) {
        return "{\"ok\":false,\"error\":"+escape_json(error.what())+"}";
    }
}
} // namespace northstarcpp
