#pragma once
// XML v3 is the only persisted .nui encoding. This decoder produces a
// normalized in-memory JSON descriptor for the existing script UI builder.
#include <boost/property_tree/xml_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <algorithm>
#include <cmath>
#include <cctype>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace northstarcpp::nui_xml {
namespace pt=boost::property_tree;
inline std::string quote(std::string_view s) {
    std::string o="\"";
    for(unsigned char c:s) {
        if(c=='"'||c=='\\'){o+='\\';o+=char(c);}
        else if(c=='\n')o+="\\n";
        else if(c=='\r')o+="\\r";
        else if(c=='\t')o+="\\t";
        else if(c<32){static char const* h="0123456789abcdef";o+="\\u00";o+=h[c>>4];o+=h[c&15];}
        else o+=char(c);
    }
    return o+"\"";
}
inline bool valid(std::string const& s) {
    if(s.empty()||s.size()>64||!std::isalpha(static_cast<unsigned char>(s.front())))return false;
    return std::all_of(s.begin(),s.end(),[](unsigned char c){
        return std::isalnum(c)||c=='_'||c=='-'||c=='.';
    });
}
inline std::string attr(pt::ptree const& p,char const* n,std::string def={}) {
    return p.get<std::string>(std::string("<xmlattr>.")+n,def);
}
inline void attrs(pt::ptree const& p,std::set<std::string> const& expected){
    if(auto a=p.get_child_optional("<xmlattr>"))
        for(auto const& [k,v]:*a)if(!expected.count(k))
            throw std::invalid_argument("Unknown .nui XML attribute: "+k);
}
inline std::string value(pt::ptree const& p,std::set<std::string> const& tokens) {
    for(auto const& [tag,child]:p)
        if(tag!="<xmlattr>" && tag!="<xmltext>")
            throw std::invalid_argument("Nested content is not supported in a UI property");
    if(std::any_of(p.data().begin(),p.data().end(),[](unsigned char c){
        return std::isspace(c)==0;
    }))throw std::invalid_argument("UI property must contain only attributes");
    auto ref=attr(p,"token");
    if(!ref.empty()) {
        if(!tokens.count(ref) || p.get_optional<std::string>("<xmlattr>.value") ||
           !attr(p,"type").empty())
            throw std::invalid_argument("Unknown or ambiguous UI token reference");
        return "{\"token\":"+quote(ref)+"}";
    }
    const auto type=attr(p,"type");
    auto input=p.get_optional<std::string>("<xmlattr>.value");
    if(!input || input->size()>2048)
        throw std::invalid_argument("Invalid UI literal length");
    if(type=="string")return quote(*input);
    if(type=="color"){
        if(input->size()!=9 || input->front()!='#' ||
           !std::all_of(input->begin()+1,input->end(),[](unsigned char c){return std::isxdigit(c)!=0;}))
            throw std::invalid_argument("UI color requires #RRGGBBAA");
        return quote(*input);
    }
    if(type=="bool"){
        if(*input!="true"&&*input!="false")
            throw std::invalid_argument("UI bool requires true or false");
        return *input;
    }
    if(type=="number"){
        std::size_t count=0;double parsed=0.;
        try{parsed=std::stod(*input,&count);}catch(...){
            throw std::invalid_argument("Invalid UI numeric literal");
        }
        if(count!=input->size() || !std::isfinite(parsed) || std::abs(parsed)>1e6)
            throw std::invalid_argument("Invalid UI numeric bounds");
        return *input;
    }
    throw std::invalid_argument("Unknown UI value type: "+type);
}
inline std::string properties(pt::ptree const& n,std::set<std::string> const& tokens){
    std::string out="{";
    std::set<std::string> known;
    for(auto const& [tag,p]:n){
        if(tag=="<xmlattr>"||tag=="<xmltext>")continue;
        if(tag!="Property")throw std::invalid_argument("Expected Property element");
        attrs(p,{"name","type","value","token"});
        auto key=attr(p,"name");
        if(!valid(key)||!known.insert(key).second)
            throw std::invalid_argument("Duplicate/invalid UI property name");
        if(p.size()!=1 || p.begin()->first!="<xmlattr>")
            throw std::invalid_argument("Property must contain only XML attributes");
        if(known.size()>48)throw std::length_error("Too many UI properties");
        if(out.size()>1)out+=",";
        out+=quote(key)+":"+value(p,tokens);
    }
    return out+"}";
}
inline std::string compile(std::string_view xml) {
    if(xml.empty() || xml.size()>262144)
        throw std::length_error("UI XML must be 1..262144 bytes");
    std::string upper(xml);
    std::transform(upper.begin(),upper.end(),upper.begin(),[](unsigned char c){return char(std::toupper(c));});
    if(upper.find("<!DOCTYPE")!=std::string::npos || upper.find("<!ENTITY")!=std::string::npos)
        throw std::invalid_argument("XML DTD/entities forbidden in .nui");
    std::istringstream input{std::string(xml)};
    pt::ptree doc;
    pt::read_xml(input,doc,pt::xml_parser::no_comments);
    if(doc.size()!=1 || doc.begin()->first!="NorthStarUI")
        throw std::invalid_argument("Expected NorthStarUI XML root");
    auto const& ui=doc.begin()->second;
    attrs(ui,{"version","target","layer","entityId","width","height","worldWidth","worldHeight"});
    if(attr(ui,"version")!="3")throw std::invalid_argument("Expected UI XML v3");
    auto target=attr(ui,"target");
    if(target!="screen"&&target!="world")throw std::invalid_argument("UI must target screen/world");
    auto result=std::string("{\"schema\":\"northstar.ui/2\",\"target\":")+quote(target);
    for(auto k:{"layer","entityId"}){
        auto v=attr(ui,k);
        if(!v.empty())result+=","+quote(k)+":"+quote(v);
    }
    for(auto k:{"width","height","worldWidth","worldHeight"}){
        auto v=attr(ui,k);
        if(!v.empty()){
            std::size_t used=0;double n=0.;
            try{n=std::stod(v,&used);}catch(...){throw std::invalid_argument("Invalid UI geometry");}
            if(used!=v.size() || !std::isfinite(n)||n<=0||n>1e6)
                throw std::invalid_argument("Invalid UI geometry");
            result+=","+quote(k)+":"+v;
        }
    }
    pt::ptree const *t=nullptr,*s=nullptr,*elements=nullptr,*components=nullptr,*actions=nullptr,*animations=nullptr,*breakpoints=nullptr;
    for(auto const& [tag,group]:ui){
        if(tag=="<xmlattr>"||tag=="<xmltext>")continue;
        if(tag=="Tokens"&&!t)t=&group;
        else if(tag=="Styles"&&!s)s=&group;
        else if(tag=="Elements"&&!elements)elements=&group;
        else if(tag=="Components"&&!components)components=&group;
        else if(tag=="Actions"&&!actions)actions=&group;
        else if(tag=="Animations"&&!animations)animations=&group;
        else if(tag=="Breakpoints"&&!breakpoints)breakpoints=&group;
        else throw std::invalid_argument("Duplicate/unknown UI section");
    }
    if(!elements)throw std::invalid_argument("Missing UI Elements section");
    std::set<std::string> tokens,styles;
    result+=",\"tokens\":{";
    if(t)for(auto const& [tag,item]:*t){
        if(tag=="<xmlattr>"||tag=="<xmltext>")continue;
        if(tag!="Token")throw std::invalid_argument("Unknown XML token");
        attrs(item,{"name","type","value"});
        auto name=attr(item,"name");
        if(!valid(name)||!tokens.insert(name).second || tokens.size()>128)
            throw std::invalid_argument("Invalid/duplicate UI token");
        if(tokens.size()>1)result+=",";
        result+=quote(name)+":"+value(item,{});
    }
    result+="},\"styles\":{";
    if(s)for(auto const& [tag,item]:*s){
        if(tag=="<xmlattr>"||tag=="<xmltext>")continue;
        if(tag!="Style")throw std::invalid_argument("Unknown XML style");
        attrs(item,{"name"});
        auto name=attr(item,"name");
        if(!valid(name)||!styles.insert(name).second || styles.size()>128)
            throw std::invalid_argument("Duplicate/invalid UI style");
        if(styles.size()>1)result+=",";
        result+=quote(name)+":"+properties(item,tokens);
    }
    result+="},\"components\":{";
    std::map<std::string,std::string> componentTypes;
    if(components)for(auto const& [tag,item]:*components){
        if(tag=="<xmlattr>"||tag=="<xmltext>")continue;
        if(tag!="Component")throw std::invalid_argument("Expected Component declaration");
        attrs(item,{"name","type"});
        auto name=attr(item,"name"),type=attr(item,"type");
        if(!valid(name)||componentTypes.count(name)||componentTypes.size()>=64)
            throw std::invalid_argument("Duplicate or invalid component");
        if(type!="panel"&&type!="text"&&type!="button"&&type!="image"&&type!="input")
            throw std::invalid_argument("Unknown component widget type");
        if(!componentTypes.empty())result+=",";
        componentTypes[name]=type;
        result+=quote(name)+":{\"type\":"+quote(type)+",\"props\":"+properties(item,tokens)+"}";
    }
    result+="},\"nodes\":[";
    static std::map<std::string,std::string> const kinds={
        {"Panel","panel"},{"Text","text"},{"Button","button"},
        {"Image","image"},{"Input","input"}};
    std::set<std::string> nodes{""};
    for(auto const& [tag,item]:*elements){
        if(tag=="<xmlattr>"||tag=="<xmltext>")continue;
        auto it=kinds.find(tag);
        auto component=attr(item,"component");
        bool reuse=tag=="Use";
        if(!reuse && it==kinds.end())throw std::invalid_argument("Unsupported UI node: "+tag);
        if(reuse && (!componentTypes.count(component)))
            throw std::invalid_argument("Unknown reusable UI component");
        attrs(item,reuse?std::set<std::string>{"id","parent","component","style"}:
                         std::set<std::string>{"id","parent","style"});
        auto name=attr(item,"id"),parent=attr(item,"parent");
        if(!valid(name)||!nodes.count(parent)||!nodes.insert(name).second ||
           nodes.size()>257)
            throw std::invalid_argument("Invalid UI node ID/hierarchy");
        if(nodes.size()>2)result+=",";
        result+="{\"id\":"+quote(name)+",\"parent\":"+quote(parent)+
                ",\"type\":"+quote(reuse?componentTypes.at(component):it->second);
        if(reuse)result+=",\"component\":"+quote(component);
        auto style=attr(item,"style");
        if(!style.empty()){
            if(!styles.count(style))throw std::invalid_argument("Missing UI named style");
            result+=",\"style\":"+quote(style);
        }
        result+=",\"props\":"+properties(item,tokens)+"}";
    }
    result+="],\"actions\":[";
    std::size_t totalActions=0;
    if(actions)for(auto const& [tag,item]:*actions){
        if(tag=="<xmlattr>"||tag=="<xmltext>")continue;
        if(tag!="Action")throw std::invalid_argument("Expected Action");
        attrs(item,{"id","event","emit","animation"});
        auto id=attr(item,"id"),event=attr(item,"event"),name=attr(item,"emit");
        if(!nodes.count(id)||name.empty()||name.size()>96||
           !std::all_of(name.begin(),name.end(),[](unsigned char c){
               return std::isalnum(c)||c=='_'||c=='.'||c==':'||c=='-';
           })|| (event!="click"&&event!="press"&&event!="change"))
            throw std::invalid_argument("Invalid declarative UI event action");
        if(++totalActions>128)throw std::length_error("Too many UI actions");
        if(totalActions>1)result+=",";
        result+="{\"id\":"+quote(id)+",\"event\":"+quote(event)+",\"emit\":"+quote(name)+
                ",\"animation\":"+quote(attr(item,"animation"))+"}";
    }
    result+="],\"animations\":[";
    std::size_t totalAnimations=0;
    std::set<std::string> animationNames;
    if(animations)for(auto const& [tag,item]:*animations){
        if(tag=="<xmlattr>"||tag=="<xmltext>")continue;
        if(tag!="Animation")throw std::invalid_argument("Expected Animation");
        attrs(item,{"name","id","property","from","to","duration","easing","trigger"});
        auto name=attr(item,"name"),id=attr(item,"id"),property=attr(item,"property");
        auto easing=attr(item,"easing","linear"),trigger=attr(item,"trigger","manual");
        if(!valid(name)||!animationNames.insert(name).second || !nodes.count(id)||
           (property!="x"&&property!="y"&&property!="width"&&property!="height"&&
            property!="font_scale") ||
           (easing!="linear"&&easing!="easeOutCubic"&&easing!="easeInOut") ||
           (trigger!="manual"&&trigger!="mount"))
            throw std::invalid_argument("Invalid declarative UI animation");
        auto number=[](std::string const& v,double min,double max){
            std::size_t pos=0;double result=0;
            try{result=std::stod(v,&pos);}catch(...){
                throw std::invalid_argument("Invalid animation number");
            }
            if(pos!=v.size() || !std::isfinite(result)||result<min||result>max)
                throw std::invalid_argument("Animation number out of bounds");
            return result;
        };
        auto from=number(attr(item,"from"),-1000000,1000000);
        auto to=number(attr(item,"to"),-1000000,1000000);
        auto duration=number(attr(item,"duration"),0.001,30);
        if(++totalAnimations>128)throw std::length_error("Too many UI animations");
        if(totalAnimations>1)result+=",";
        result+="{\"name\":"+quote(name)+",\"id\":"+quote(id)+",\"property\":"+quote(property)+
                ",\"from\":"+std::to_string(from)+",\"to\":"+std::to_string(to)+
                ",\"duration\":"+std::to_string(duration)+",\"easing\":"+quote(easing)+
                ",\"trigger\":"+quote(trigger)+"}";
    }
    result+="],\"breakpoints\":[";
    std::size_t totalBreakpoints=0;
    if(breakpoints)for(auto const& [tag,item]:*breakpoints){
        if(tag=="<xmlattr>"||tag=="<xmltext>")continue;
        if(tag!="Breakpoint")throw std::invalid_argument("Expected Breakpoint");
        attrs(item,{"minWidth","maxWidth"});
        auto integer=[](std::string const& value){
            if(value.empty()||!std::all_of(value.begin(),value.end(),[](unsigned char c){return std::isdigit(c)!=0;}))
                throw std::invalid_argument("Invalid breakpoint width");
            auto n=std::stoul(value);
            if(n>16384)throw std::invalid_argument("Breakpoint exceeds 16384 px");
            return n;
        };
        auto low=integer(attr(item,"minWidth","0")),high=integer(attr(item,"maxWidth","16384"));
        if(low>high)throw std::invalid_argument("Breakpoint minWidth > maxWidth");
        if(++totalBreakpoints>64)throw std::length_error("Too many UI breakpoints");
        if(totalBreakpoints>1)result+=",";
        result+="{\"minWidth\":"+std::to_string(low)+",\"maxWidth\":"+std::to_string(high)+",\"overrides\":{";
        std::set<std::string> overrides;
        for(auto const& [kind,override]:item){
            if(kind=="<xmlattr>"||kind=="<xmltext>")continue;
            if(kind!="Override")throw std::invalid_argument("Expected Override element");
            attrs(override,{"id"});
            auto id=attr(override,"id");
            if(!nodes.count(id)||!overrides.insert(id).second)
                throw std::invalid_argument("Unknown/duplicate breakpoint target");
            if(overrides.size()>128)throw std::length_error("Too many breakpoint overrides");
            if(overrides.size()>1)result+=",";
            result+=quote(id)+":"+properties(override,tokens);
        }
        result+="}}";
    }
    result+="]}";
    if(result.size()>262144)throw std::length_error("Compiled UI descriptor too large");
    return result;
}
} // namespace northstarcpp::nui_xml
