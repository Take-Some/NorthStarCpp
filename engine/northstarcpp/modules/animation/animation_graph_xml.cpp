#include <northstarcpp/animation_graph.hpp>
#include <northstarcpp/assets.hpp>
#include <boost/property_tree/xml_parser.hpp>
#include <algorithm>
#include <cmath>
#include <sstream>
#include <stdexcept>
#include <unordered_set>
namespace northstarcpp {
namespace {
using Tree=boost::property_tree::ptree;
void verify(bool b,char const* why){if(!b)throw std::invalid_argument(why);}
std::string attr(Tree const& n,char const* key){
    return n.get<std::string>(std::string("<xmlattr>.")+key);
}
float number(Tree const& n,char const* key,float fallback) {
    auto v=n.get<float>(std::string("<xmlattr>.")+key,fallback);
    verify(std::isfinite(v),"Nonfinite graph numeric attribute");return v;
}
void identifier(std::string const& s) {
    verify(!s.empty() && s.size()<=128,"Bad graph identifier");
    for(unsigned char c:s)
        verify(std::isalnum(c)||c=='_'||c=='-'||c=='.',"Bad identifier character");
}
void reference(std::string const& s) {
    auto at=s.rfind('@');
    verify(s.size()<=512 && at!=std::string::npos && at>=4 &&
        s.substr(at-4,4)==".ycd" && at+1<s.size() &&
        s.find("..")==std::string::npos &&
        s.find('\\')==std::string::npos && s.find(':')==std::string::npos,
        "Expected safe YCD@clip reference");
}
}
std::vector<std::string> AnimationGraph::references() const {
    std::vector<std::string> result;
    std::unordered_set<std::string> seen;
    auto add=[&](std::string const& s){
        if(!s.empty() && seen.insert(s).second)result.push_back(s);
    };
    for(auto const& state:states) {
        add(state.clip);for(auto const& p:state.points)add(p.clip);
    }
    for(auto const& layer:layers)add(layer.clip);
    return result;
}
AnimationGraph parse_animation_graph_xml(std::string const& xml) {
    verify(!xml.empty() && xml.size()<=1048576 && xml.find('\0')==std::string::npos,
        "Invalid graph XML size");
    Tree doc;std::istringstream input(xml);
    boost::property_tree::read_xml(input,doc,boost::property_tree::xml_parser::no_comments);
    auto const& root=doc.get_child("AnimationGraph");
    verify(root.get<int>("<xmlattr>.schema")==1,"Unsupported graph XML schema");
    AnimationGraph result;
    result.initial=attr(root,"initial");identifier(result.initial);
    for(auto const& section:root) {
        if(section.first=="<xmlattr>")continue;
        for(auto const& node:section.second) {
            if(node.first=="<xmlattr>")continue;
            auto const& n=node.second;
            if(section.first=="Parameters") {
                verify(node.first=="Float" && result.defaults.size()<64,
                    "Invalid graph parameters");
                auto id=attr(n,"name");identifier(id);
                verify(result.defaults.emplace(id,number(n,"default",0.f)).second,
                    "Duplicate graph parameter");
            } else if(section.first=="States") {
                verify(node.first=="State" && result.states.size()<128,
                    "Invalid graph states");
                AnimationGraphState s;
                s.name=attr(n,"name");identifier(s.name);
                s.sync_group=n.get<std::string>("<xmlattr>.sync","");
                if(!s.sync_group.empty())identifier(s.sync_group);
                for(auto const& data:n) {
                    if(data.first=="<xmlattr>")continue;
                    verify(s.clip.empty()&&s.points.empty(),
                        "Graph state requires one source");
                    if(data.first=="Clip") {
                        s.clip=attr(data.second,"ref");reference(s.clip);
                    } else if(data.first=="Blend1D") {
                        s.parameter=attr(data.second,"parameter");identifier(s.parameter);
                        for(auto const& pt:data.second) {
                            if(pt.first=="<xmlattr>")continue;
                            verify(pt.first=="Point" && s.points.size()<32,
                                "Invalid Blend1D points");
                            AnimationBlendPoint p;
                            p.at=number(pt.second,"at",0);
                            p.clip=attr(pt.second,"ref");reference(p.clip);
                            s.points.push_back(std::move(p));
                        }
                        verify(s.points.size()>=2,"Blend1D requires two points");
                        std::sort(s.points.begin(),s.points.end(),
                            [](auto const& a,auto const& b){return a.at<b.at;});
                        for(std::size_t i=1;i<s.points.size();++i)
                            verify(s.points[i-1].at<s.points[i].at,
                                "Duplicate Blend1D threshold");
                    } else throw std::invalid_argument("Unknown state source");
                }
                verify(!s.clip.empty()||!s.points.empty(),"Empty graph state");
                result.states.push_back(std::move(s));
            } else if(section.first=="Transitions") {
                verify(node.first=="Transition" && result.transitions.size()<512,
                    "Invalid graph transition count");
                AnimationGraphTransition t;
                t.from=attr(n,"from");t.to=attr(n,"to");t.parameter=attr(n,"parameter");
                identifier(t.from);identifier(t.to);identifier(t.parameter);
                auto op=n.get<std::string>("<xmlattr>.op","ge");
                verify(op=="ge"||op=="le","Unknown transition comparison");
                t.operation=op=="ge"?AnimationTransitionOp::greater_equal:
                    AnimationTransitionOp::less_equal;
                t.threshold=number(n,"value",0.f);
                t.fade_seconds=number(n,"fade",.15f);
                verify(t.fade_seconds>=0.f&&t.fade_seconds<=10.f,
                    "Invalid transition fade");
                result.transitions.push_back(std::move(t));
            } else if(section.first=="Layers") {
                verify(node.first=="Layer" && result.layers.size()<16,
                    "Invalid graph layer count");
                AnimationGraphLayer l;
                l.name=attr(n,"name");l.clip=attr(n,"ref");
                identifier(l.name);reference(l.clip);
                l.parameter=n.get<std::string>("<xmlattr>.weight_parameter","");
                if(!l.parameter.empty())identifier(l.parameter);
                l.weight=number(n,"weight",1.f);
                verify(l.weight>=0.f&&l.weight<=1.f,"Invalid layer weight");
                auto mode=n.get<std::string>("<xmlattr>.mode","override");
                verify(mode=="override"||mode=="additive","Invalid layer mode");
                l.mode=mode=="additive"?AnimationLayerMode::additive:
                    AnimationLayerMode::override_pose;
                auto roots=n.get<std::string>("<xmlattr>.roots","");
                std::istringstream ss(roots);std::string bone;
                while(std::getline(ss,bone,',')) {
                    auto begin=bone.find_first_not_of(" \t");
                    verify(begin!=std::string::npos&&l.bone_roots.size()<64,
                        "Invalid layer bone mask");
                    bone=bone.substr(begin,bone.find_last_not_of(" \t")-begin+1);
                    identifier(bone);l.bone_roots.push_back(bone);
                }
                result.layers.push_back(std::move(l));
            } else if(section.first=="Events") {
                verify(node.first=="Notify"&&result.markers.size()<512,
                    "Invalid graph event count");
                AnimationGraphMarker m;
                m.clip=attr(n,"ref");m.name=attr(n,"name");
                reference(m.clip);
                // Namespaced notify for rig-authored foot phases. All other
                // identifiers preserve the strict legacy grammar.
                auto const plant=m.name.rfind("ik.foot.plant:",0)==0;
                auto const lift=m.name.rfind("ik.foot.lift:",0)==0;
                if(plant)identifier(m.name.substr(14));
                else if(lift)identifier(m.name.substr(13));
                else identifier(m.name);
                m.normalized_time=number(n,"at",0.f);
                verify(m.normalized_time>=0.f&&m.normalized_time<=1.f,
                    "Event normalized time outside [0,1]");
                result.markers.push_back(std::move(m));
            } else throw std::invalid_argument("Unknown graph section");
        }
    }
    verify(!result.states.empty(),"Empty graph");
    std::unordered_set<std::string> states,layers;
    for(auto const& state:result.states) {
        verify(states.insert(state.name).second,"Duplicate state");
        verify(state.parameter.empty()||result.defaults.count(state.parameter),
            "Unknown blend parameter");
    }
    verify(states.count(result.initial)!=0,"Missing initial state");
    for(auto const& t:result.transitions)
        verify(states.count(t.from)&&states.count(t.to)&&result.defaults.count(t.parameter),
            "Bad transition state/parameter");
    for(auto const& l:result.layers)
        verify(layers.insert(l.name).second &&
            (l.parameter.empty()||result.defaults.count(l.parameter)),
            "Bad layer/weight parameter");
    auto refs=result.references();
    for(auto const& e:result.markers)
        verify(std::find(refs.begin(),refs.end(),e.clip)!=refs.end(),
            "Event reference does not belong to graph");
    return result;
}
AnimationGraph load_animation_graph(AssetStore const& assets,std::string const& path) {
    verify(!path.empty()&&path.size()<=512 &&
        path.find("..")==std::string::npos &&
        path.find('\\')==std::string::npos &&
        path.find(':')==std::string::npos,"Unsafe graph XML asset path");
    verify(assets.size(path)<=1048576,"Oversized graph XML");
    auto bytes=assets.read(path);
    return parse_animation_graph_xml(std::string(bytes.begin(),bytes.end()));
}
}
