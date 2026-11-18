// Source-authored sky-layered.fx binding map translated to shared engine.sky layers.
// Native YTD entries are decoded through the same BCn path as MaterialDefinition.
#pragma once
#include <northstarcpp/assets.hpp>
#include <northstarcpp/material.hpp>
#include <northstarcpp/sky_environment.hpp>
#include <boost/property_tree/xml_parser.hpp>
#include <array>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace northstarcpp::viewport_detail {
inline SkyTextureLayers load_sky_source_layers(
    AssetStore const& assets,std::string const& requested_preset)
{
    constexpr char manifest[]="textures/sky/sky_sources.xml";
    auto const encoded=assets.read(manifest);
    if(encoded.empty() || encoded.size()>512*1024u)
        throw std::runtime_error("Invalid sky source binding manifest size");
    std::istringstream stream(
        std::string(encoded.begin(),encoded.end()));
    boost::property_tree::ptree xml;
    boost::property_tree::read_xml(stream,xml);
    auto const& root=xml.get_child("NorthStarSkySources");
    std::map<std::string,std::string> selected,fallback;
    bool found_preset=false;
    for(auto const& [tag,node]:root) {
        if(tag!="Preset")continue;
        auto const id=node.get<std::string>("<xmlattr>.id","");
        if(id==requested_preset)found_preset=true;
        for(auto const& [name,binding]:node) {
            if(name!="Binding")continue;
            auto const slot=binding.get<std::string>("<xmlattr>.slot","");
            auto const reference=binding.get<std::string>(
                "<xmlattr>.texture","");
            if(reference.empty())continue; // debug-grid utility
            if(reference.rfind("textures/sky/",0)!=0 ||
               reference.find(".ytd@tex_")==std::string::npos)
                throw std::runtime_error("Sky source manifest has unsafe YTD ref");
            fallback.emplace(slot,reference);
            if(id==requested_preset)selected.emplace(slot,reference);
        }
    }
    if(!found_preset)
        throw std::runtime_error("Unknown authored sky preset: "+requested_preset);
    auto choose=[&](std::initializer_list<char const*> slots) ->std::string {
        for(auto const* key:slots)
            if(auto it=selected.find(key);it!=selected.end())
                return it->second;
        for(auto const* key:slots)
            if(auto it=fallback.find(key);it!=fallback.end())
                return it->second;
        throw std::runtime_error("Original sky-layered.fx texture binding missing");
    };
    SkyTextureLayers layers;
    auto load=[&](std::string const& ref)->SkyLayerImage {
        auto image=load_native_texture_image(assets,ref);
        SkyLayerImage out{image.width,image.height,std::move(image.rgba8)};
        if(!out.valid())throw std::runtime_error(
            "Sky source YTD image is missing decoded RGBA8 pixels");
        return out;
    };
    layers.atmosphere=load(choose({
        "g_tNdFetchColorHdr01Map","g_tNdFetchColorHdr02Map",
        "g_tNdFetchColorHdr04Map"}));
    layers.cloud_projection=load(choose({
        "g_tNdFetchCloudProjectionMap","g_tNdFetchCloudProjectionWideMap",
        "g_tNdFetchCloudMaskMap"}));
    layers.flow_phase=load(choose({
        "g_tNdFetchFlowPhase01Map","g_tNdFetchFlowPhase05Map",
        "g_tNdFetchFlowPhase06Map"}));
    layers.cloud_breakup=load(choose({
        "g_tNdFetchFlowBreakup01Map","g_tNdFetchFlowBreakup02Map",
        "g_tNdFetchFlowBreakup04Map"}));
    if(!layers.valid())
        throw std::runtime_error("Missing original sky/cloud image layers");
    return layers;
}
} // namespace northstarcpp::viewport_detail
