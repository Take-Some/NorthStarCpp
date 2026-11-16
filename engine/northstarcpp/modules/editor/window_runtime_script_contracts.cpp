#include "window_runtime_script_contracts.hpp"
#include "../viewport/viewport.hpp"
#include <northstarcpp/scripting.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <set>
#include <sstream>
#include <stdexcept>
#include <unordered_set>
#include <utility>
namespace northstarcpp::runtime_script_contracts {
namespace fs = std::filesystem;
std::vector<std::string> script_args(std::string_view json) {
    if (json.empty() || json.front()!='[' ||
        json.size()>NORTHSTAR_SCRIPTING_MAX_SOURCE_BYTES)
        throw std::invalid_argument("Invalid engine API argument array");
    boost::property_tree::ptree root;
    std::istringstream in{std::string(json)};
    boost::property_tree::read_json(in,root);
    std::vector<std::string> result;
    for (auto const& entry : root) {
        if (!entry.first.empty() || result.size()>32)
            throw std::invalid_argument("Invalid engine API argument");
        result.push_back(entry.second.get_value<std::string>());
    }
    return result;
}
// A single game-agnostic UI entrypoint. All node IDs, text, colors, and
// behavior are supplied by the script; native code only validates structure.
UiDocument parse_script_ui_document(std::string_view args_json) {
    if(args_json.empty() || args_json.size()>262144 ||
       args_json.front()!='[')
        throw std::invalid_argument("ui.apply expects one bounded document");
    boost::property_tree::ptree args;
    std::istringstream stream{std::string(args_json)};
    boost::property_tree::read_json(stream,args);
    if(args.size()!=1 || !args.begin()->first.empty())
        throw std::invalid_argument("ui.apply(document) expects one argument");
    auto const& document=args.begin()->second;
    auto const target=document.get<std::string>("target","screen");
    if(target!="screen" && target!="world")
        throw std::invalid_argument("UI target must be screen or world");
    UiDocument result;
    result.target=target;
    if(target=="world") {
        result.entity_id=document.get<std::string>("entityId","");
        if(result.entity_id.empty() || result.entity_id.size()>32 ||
           !std::all_of(result.entity_id.begin(),result.entity_id.end(),
             [](unsigned char c){return c>='0'&&c<='9';}))
            throw std::invalid_argument("World UI needs a stable decimal Entity ID");
        result.width=document.get<std::uint32_t>("width",512);
        result.height=document.get<std::uint32_t>("height",256);
        result.world_width=document.get<float>("worldWidth",1.8f);
        result.world_height=document.get<float>("worldHeight",1.f);
        if(result.width<32 || result.width>1024 ||
           result.height<32 || result.height>1024 ||
           static_cast<std::uint64_t>(result.width)*result.height>524288 ||
           !std::isfinite(result.world_width) || !std::isfinite(result.world_height) ||
           result.world_width<0.1f || result.world_width>20.f ||
           result.world_height<0.1f || result.world_height>20.f)
            throw std::invalid_argument("Invalid world UI texture or geometry dimensions");
    }
    auto nodes=document.get_child_optional("nodes");
    if(!nodes)throw std::invalid_argument("UI document needs nodes array");
    if(nodes->size()>256)
        throw std::length_error("UI document exceeds 256 elements");
    result.nodes.reserve(nodes->size());
    std::unordered_set<std::string> declared{""};
    auto identifier=[](std::string const& key) {
        return !key.empty() && key.size()<=96 &&
            std::all_of(key.begin(),key.end(),[](unsigned char c){
                return (c>='A'&&c<='Z') || (c>='a'&&c<='z') ||
                    (c>='0'&&c<='9') || c=='_' || c=='-' || c=='.';
            });
    };
    for(auto const& entry:*nodes) {
        if(!entry.first.empty())
            throw std::invalid_argument("UI nodes must be an array");
        auto const& item=entry.second;
        UiDocumentNode node;
        node.id=item.get<std::string>("id");
        node.parent=item.get<std::string>("parent","");
        auto kind=item.get<std::string>("type");
        if(!identifier(node.id) || !declared.insert(node.id).second ||
           !declared.count(node.parent))
            throw std::invalid_argument("UI node ID or parent is invalid");
        if(kind=="panel")node.kind=NORTHSTAR_UI_PANEL;
        else if(kind=="text")node.kind=NORTHSTAR_UI_TEXT;
        else if(kind=="button")node.kind=NORTHSTAR_UI_BUTTON;
        else if(kind=="image")node.kind=NORTHSTAR_UI_IMAGE;
        else if(kind=="input")node.kind=NORTHSTAR_UI_INPUT;
        else throw std::invalid_argument("Unsupported UI node type");
        if(auto props=item.get_child_optional("props")) {
            if(props->size()>48)
                throw std::length_error("UI node has too many properties");
            for(auto const& prop:*props) {
                if(!identifier(prop.first) || prop.first.size()>64)
                    throw std::invalid_argument("Invalid UI property name");
                UiPropertyValue value;
                auto const& typed=prop.second;
                auto const type=typed.get<std::string>("type");
                auto const raw=typed.get<std::string>("value");
                if(type=="text") {
                    if(raw.size()>2048)
                        throw std::length_error("UI text exceeds 2048 bytes");
                    value.kind=NORTHSTAR_UI_VALUE_STRING;
                    value.text=raw;
                } else if(type=="number") {
                    std::size_t pos=0;
                    value.number=std::stod(raw,&pos);
                    if(pos!=raw.size() || !std::isfinite(value.number) ||
                       std::abs(value.number)>1e6)
                        throw std::invalid_argument("Invalid UI numeric property");
                    value.kind=NORTHSTAR_UI_VALUE_NUMBER;
                } else if(type=="bool") {
                    if(raw!="true" && raw!="false")
                        throw std::invalid_argument("Invalid UI boolean property");
                    value.kind=NORTHSTAR_UI_VALUE_BOOL;
                    value.boolean=raw=="true";
                } else if(type=="color") {
                    if(raw.size()!=9 || raw[0]!='#' ||
                       !std::all_of(raw.begin()+1,raw.end(),
                           [](unsigned char c){return std::isxdigit(c)!=0;}))
                        throw std::invalid_argument("UI color needs #RRGGBBAA");
                    value.kind=NORTHSTAR_UI_VALUE_COLOR_RGBA8;
                    value.rgba=static_cast<std::uint32_t>(
                        std::stoul(raw.substr(1),nullptr,16));
                } else throw std::invalid_argument("Invalid UI property type");
                node.properties.emplace_back(prop.first,std::move(value));
            }
        }
        result.nodes.push_back(std::move(node));
    }
    return result;
}
std::size_t script_index(std::string const& value) {
    if (value.empty() || std::any_of(value.begin(),value.end(),
        [](unsigned char ch){return ch<'0' || ch>'9';}))
        throw std::invalid_argument("Script entity index must be unsigned decimal");
    std::size_t end=0;
    auto const index=std::stoull(value,&end);
    if (end!=value.size()) throw std::invalid_argument("Invalid index");
    return static_cast<std::size_t>(index);
}
double script_number(std::string const& value) {
    std::size_t end=0;
    auto number=std::stod(value,&end);
    if (end!=value.size() || !std::isfinite(number))
        throw std::invalid_argument("Invalid scripting number");
    return number;
}

// Structured character descriptors are validated as JSON objects rather than
// flattened by script_args(). The bridge never accepts arbitrary host paths.
CharacterAssetSpec parse_script_character(std::string_view json) {
    if(json.empty() || json.size()>NORTHSTAR_SCRIPTING_MAX_SOURCE_BYTES)
        throw std::invalid_argument("Character descriptor exceeds scripting limit");
    boost::property_tree::ptree array;
    std::istringstream stream{std::string(json)};
    boost::property_tree::read_json(stream,array);
    if(array.size()!=1 || !array.front().first.empty())
        throw std::invalid_argument("character.load(descriptor) expects one object");
    auto const& definition=array.front().second;
    auto path=[](std::string const& value)->std::string {
        auto const input=fs::u8path(value);
        if(value.empty() || value.size()>240 || input.is_absolute() ||
           value.find(':')!=std::string::npos ||
           value.find('\\')!=std::string::npos)
            throw std::invalid_argument("Character asset must use a relative VFS path");
        for(auto const& part:input)
            if(part.empty() || part=="." || part=="..")
                throw std::invalid_argument("Character asset path escapes its VFS");
        return value;
    };
    CharacterAssetSpec descriptor;
    descriptor.model=path(definition.get<std::string>("model"));
    descriptor.skeleton=path(definition.get<std::string>("skeleton"));
    descriptor.material=path(definition.get<std::string>("material"));
    descriptor.textures=path(definition.get<std::string>("textures"));
    auto const animations=definition.get_child_optional("animations");
    if(!animations || animations->empty() || animations->size()>32)
        throw std::invalid_argument("Character requires 1..32 named animation clips");
    std::set<std::string> names;
    for(auto const& [key,value]:*animations) {
        if(!key.empty()) throw std::invalid_argument("Animations must be an array");
        CharacterAnimationAsset item;
        item.name=value.get<std::string>("name");
        item.source=path(value.get<std::string>("source"));
        item.clip=value.get<std::string>("clip");
        if(item.name.empty() || item.name.size()>64 || item.clip.empty() ||
           item.clip.size()>160 || !names.insert(item.name).second)
            throw std::invalid_argument("Invalid or duplicate skeletal animation name");
        descriptor.animations.push_back(std::move(item));
    }
    return descriptor;
}

 } // namespace northstarcpp::runtime_script_contracts
