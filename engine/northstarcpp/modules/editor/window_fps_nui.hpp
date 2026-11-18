// Standalone FPS .nui XML v3 -> typed, plugin-neutral UiDocument.
// Source is always XML. Property types are preserved exactly, including
// numeric-looking strings and literal "true"/"false" captions.
#pragma once
#include "../viewport/ui_document.hpp"
#include "../shared/ui_xml_codec.hpp"
#include <boost/property_tree/xml_parser.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace northstarcpp::fps_nui {
namespace pt=boost::property_tree;
struct Literal {
    std::string type;
    std::string text;
};
inline Literal literal(pt::ptree const& property,
                       std::map<std::string,Literal> const& tokens) {
    auto const token=nui_xml::attr(property,"token");
    if(!token.empty()) {
        auto found=tokens.find(token);
        if(found==tokens.end())
            throw std::invalid_argument("Undefined FPS UI token: "+token);
        return found->second;
    }
    auto kind=nui_xml::attr(property,"type");
    auto raw=property.get_optional<std::string>("<xmlattr>.value");
    if(!raw)throw std::invalid_argument("Missing UI literal");
    return {kind,*raw};
}
inline UiPropertyValue typed(Literal const& data) {
    UiPropertyValue value;
    if(data.type=="string") {
        value.kind=NORTHSTAR_UI_VALUE_STRING;
        value.text=data.text;
    }else if(data.type=="color") {
        value.kind=NORTHSTAR_UI_VALUE_COLOR_RGBA8;
        value.rgba=static_cast<std::uint32_t>(
            std::stoul(data.text.substr(1),nullptr,16));
    }else if(data.type=="number") {
        value.kind=NORTHSTAR_UI_VALUE_NUMBER;
        value.number=std::stod(data.text);
    }else if(data.type=="bool") {
        value.kind=NORTHSTAR_UI_VALUE_BOOL;
        value.boolean=data.text=="true";
    }else throw std::invalid_argument("Unknown typed UI property");
    return value;
}
inline UiDocument decode(std::string_view raw,std::string const& prefix="fpspause",
                         unsigned viewportWidth=1920) {
    if(!nui_xml::valid(prefix))
        throw std::invalid_argument("Invalid FPS UI root name");
    // One canonical validator for both C++ and script-operated Player paths.
    (void)nui_xml::compile(raw);
    std::istringstream stream{std::string(raw)};
    pt::ptree xml;
    pt::read_xml(stream,xml,pt::xml_parser::no_comments);
    auto const& root=xml.get_child("NorthStarUI");
    if(nui_xml::attr(root,"target")!="screen")
        throw std::invalid_argument("Standalone FPS UI must target screen");
    std::map<std::string,Literal> tokens;
    std::map<std::string,std::map<std::string,Literal>> styles;
    if(auto group=root.get_child_optional("Tokens")) {
        for(auto const& [tag,element]:*group) {
            if(tag!="Token")continue;
            auto const key=nui_xml::attr(element,"name");
            tokens.emplace(key,literal(element,{}));
        }
    }
    if(auto group=root.get_child_optional("Styles")) {
        for(auto const& [tag,element]:*group) {
            if(tag!="Style")continue;
            std::map<std::string,Literal> properties;
            for(auto const& [propTag,prop]:element) {
                if(propTag!="Property")continue;
                properties[nui_xml::attr(prop,"name")]=literal(prop,tokens);
            }
            styles.emplace(nui_xml::attr(element,"name"),std::move(properties));
        }
    }
    struct Component { std::string kind; std::map<std::string,Literal> props; };
    std::map<std::string,Component> components;
    if(auto group=root.get_child_optional("Components")) {
        for(auto const& [tag,entry]:*group) {
            if(tag!="Component")continue;
            Component component;
            const auto name=nui_xml::attr(entry,"name");
            component.kind=nui_xml::attr(entry,"type");
            for(auto const& [key,prop]:entry)
                if(key=="Property")
                    component.props[nui_xml::attr(prop,"name")]=literal(prop,tokens);
            components.emplace(name,std::move(component));
        }
    }
    UiDocument document;
    document.target="screen";
    auto const& elements=root.get_child("Elements");
    for(auto const& [tag,element]:elements) {
        if(tag=="<xmlattr>"||tag=="<xmltext>")continue;
        UiDocumentNode node;
        auto id=nui_xml::attr(element,"id");
        auto parent=nui_xml::attr(element,"parent");
        node.id=prefix+"."+id;
        node.parent=parent.empty()?std::string{}:prefix+"."+parent;
        auto kind=tag;
        if(tag=="Use"){
            auto name=nui_xml::attr(element,"component");
            auto it=components.find(name);
            if(it==components.end())throw std::invalid_argument("Unknown FPS component");
            auto type=it->second.kind;
            kind=type=="panel"?"Panel":type=="text"?"Text":
                 type=="button"?"Button":type=="image"?"Image":"Input";
        }
        if(kind=="Panel")node.kind=NORTHSTAR_UI_PANEL;
        else if(kind=="Text")node.kind=NORTHSTAR_UI_TEXT;
        else if(kind=="Button")node.kind=NORTHSTAR_UI_BUTTON;
        else if(kind=="Image")node.kind=NORTHSTAR_UI_IMAGE;
        else if(kind=="Input")node.kind=NORTHSTAR_UI_INPUT;
        else throw std::invalid_argument("Unknown FPS UI element");
        std::map<std::string,Literal> properties;
        if(tag=="Use")properties=components.at(nui_xml::attr(element,"component")).props;
        auto style=nui_xml::attr(element,"style");
        if(!style.empty())
            for(auto const& [key,val]:styles.at(style))
                properties[key]=val;
        for(auto const& [propTag,prop]:element) {
            if(propTag!="Property")continue;
            properties[nui_xml::attr(prop,"name")]=literal(prop,tokens);
        }
        if(properties.size()>48)
            throw std::length_error("Too many merged FPS UI properties");
        for(auto const& [key,val]:properties)
            node.properties.emplace_back(key,typed(val));
        document.nodes.push_back(std::move(node));
    }
    if(auto group=root.get_child_optional("Breakpoints")) {
        for(auto const& [tag,rule]:*group) {
            if(tag!="Breakpoint")continue;
            auto min=std::stoul(nui_xml::attr(rule,"minWidth","0"));
            auto max=std::stoul(nui_xml::attr(rule,"maxWidth","16384"));
            if(viewportWidth<min || viewportWidth>max)continue;
            for(auto const& [key,entry]:rule){
                if(key!="Override")continue;
                auto id=prefix+"."+nui_xml::attr(entry,"id");
                auto node=std::find_if(document.nodes.begin(),document.nodes.end(),
                    [&](UiDocumentNode const& item){return item.id==id;});
                if(node==document.nodes.end())
                    throw std::invalid_argument("Invalid FPS breakpoint target");
                for(auto const& [propTag,prop]:entry){
                    if(propTag!="Property")continue;
                    auto name=nui_xml::attr(prop,"name");
                    auto data=typed(literal(prop,tokens));
                    auto it=std::find_if(node->properties.begin(),node->properties.end(),
                        [&](auto const& item){return item.first==name;});
                    if(it==node->properties.end())
                        node->properties.emplace_back(name,std::move(data));
                    else it->second=std::move(data);
                }
            }
        }
    }
    return document;
}
inline void text(UiDocument& doc,std::string const& id,std::string const& caption) {
    for(auto& node:doc.nodes)if(node.id==id) {
        for(auto& [key,val]:node.properties)if(key=="text") {
            val.kind=NORTHSTAR_UI_VALUE_STRING;
            val.text=caption;
            val.number=0;val.boolean=false;val.rgba=0;
            return;
        }
        UiPropertyValue val;
        val.kind=NORTHSTAR_UI_VALUE_STRING;
        val.text=caption;
        node.properties.emplace_back("text",std::move(val));
        return;
    }
    throw std::out_of_range("Unknown .nui label: "+id);
}
} // namespace northstarcpp::fps_nui
