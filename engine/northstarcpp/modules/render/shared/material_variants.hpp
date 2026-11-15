// NorthStarCpp provider-neutral Material Graph HLSL variant validation.
// Only the engine graph compiler's expression-only #define output is accepted.
#pragma once
#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <string>
#include <string_view>
namespace northstarcpp::shaders {
inline std::string material_shader_prefix(const char* bytes,unsigned count,
                                          unsigned blend,float cutoff)
{
    if(blend>2 || !(cutoff>=0.f && cutoff<=1.f))
        throw std::invalid_argument("Invalid material blend policy");
    if(count>16384 || (count&&!bytes))
        throw std::invalid_argument("Invalid Material Graph shader byte count");
    std::string result="#define NS_BLEND_MODE "+std::to_string(blend)+"\n";
    result+="#define NS_ALPHA_CUTOFF "+std::to_string(cutoff)+"\n";
    if(!count)return result;
    std::string shader(bytes,bytes+count);
    std::size_t pos=0;
    unsigned lines=0;
    while(pos<shader.size()) {
        auto end=shader.find('\n',pos);
        if(end==std::string::npos)end=shader.size();
        auto line=shader.substr(pos,end-pos);
        static constexpr std::string_view allowed[]{
            "#define NS_GRAPH_BASE(p) (",
            "#define NS_GRAPH_ROUGH(p) (",
            "#define NS_GRAPH_METAL(p) (",
            "#define NS_GRAPH_EMISSIVE(p) (",
            "#define NS_GRAPH_OPACITY(p) ("};
        if(++lines>5 || line.size()>8192 ||
           std::none_of(std::begin(allowed),std::end(allowed),
               [&](std::string_view item){return line.rfind(item,0)==0;}))
            throw std::invalid_argument("Material Graph shader has invalid output directive");
        for(std::size_t i=0;i<line.size();++i) {
            auto const c=line[i];
            // The validated #define directive must start with '#', but
            // expression bodies must never contain extra preprocessor syntax.
            if(i==0 && c=='#')continue;
            if(!std::isalnum(static_cast<unsigned char>(c)) &&
               std::string_view(" _.,()+-*/?:<>=!&|").find(c)==std::string_view::npos)
                throw std::invalid_argument("Invalid generated HLSL graph expression");
        }
        result+=line+"\n";
        pos=end+1;
    }
    return result;
}
}
