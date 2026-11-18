#include "../engine/northstarcpp/modules/editor/window_fps_nui.hpp"
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>

using northstarcpp::fps_nui::decode;
int main(int argc,char** argv) {
    if(argc!=2)return 2;
    try {
        std::ifstream file(argv[1],std::ios::binary);
        if(!file)throw std::runtime_error("missing fps_pause.nui");
        std::string bytes((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());
        auto doc=decode(bytes);
        if(doc.target!="screen" || doc.nodes.size()<5)
            throw std::runtime_error("FPS UI is not a screen with controls");
        bool resume=false,restart=false,exit=false,center=false,styled=false;
        for(auto const& node:doc.nodes) {
            resume|=node.id=="fpspause.resume";
            restart|=node.id=="fpspause.restart";
            exit|=node.id=="fpspause.exit";
            for(auto const& [name,value]:node.properties) {
                if(node.id=="fpspause.card" &&
                   name=="anchor_center_x" && value.boolean)center=true;
                if(node.id=="fpspause.resume" &&
                   name=="width" && value.number==330.)styled=true;
            }
        }
        if(!resume||!restart||!exit||!center||!styled)
            throw std::runtime_error("FPS UI IDs/layout/style are incomplete");
        northstarcpp::fps_nui::text(doc,"fpspause.caption","GAME OVER");
        const auto typed=northstarcpp::fps_nui::decode(R"(<NorthStarUI version="3" target="screen">
            <Tokens><Token name="literal" type="string" value="0012"/></Tokens>
            <Elements><Text id="test">
                <Property name="text" type="string" value="true"/>
                <Property name="code" token="literal"/>
                <Property name="visible" type="bool" value="true"/>
            </Text></Elements></NorthStarUI>)");
        if(typed.nodes.size()!=1)throw std::runtime_error("Typed XML UI missing node");
        bool textString=false,codeString=false,visibleBool=false;
        for(auto const& [key,v]:typed.nodes[0].properties) {
            if(key=="text")textString=v.kind==NORTHSTAR_UI_VALUE_STRING&&v.text=="true";
            if(key=="code")codeString=v.kind==NORTHSTAR_UI_VALUE_STRING&&v.text=="0012";
            if(key=="visible")visibleBool=v.kind==NORTHSTAR_UI_VALUE_BOOL&&v.boolean;
        }
        if(!textString||!codeString||!visibleBool)
            throw std::runtime_error("Typed FPS UI XML values were coerced");
        const auto responsive=northstarcpp::fps_nui::decode(R"(<NorthStarUI version="3" target="screen">
          <Components><Component name="action" type="button">
            <Property name="width" type="number" value="320"/>
            <Property name="text" type="string" value="DEFAULT"/>
          </Component></Components>
          <Elements><Use id="start" component="action">
            <Property name="text" type="string" value="START"/>
          </Use></Elements>
          <Breakpoints><Breakpoint maxWidth="720"><Override id="start">
            <Property name="width" type="number" value="240"/>
          </Override></Breakpoint></Breakpoints>
        </NorthStarUI>)","test",500);
        bool compact=false,caption=false;
        for(auto const& [name,value]:responsive.nodes.at(0).properties) {
            compact|=name=="width" && value.number==240.;
            caption|=name=="text" && value.text=="START";
        }
        if(!compact||!caption)
            throw std::runtime_error("FPS component + breakpoint failed");
        try {decode(R"(<NorthStarUI version="3" target="screen"><Elements><Button id="unsafe" parent="missing"/></Elements></NorthStarUI>)");
              throw std::runtime_error("Invalid parent was accepted");
        } catch(std::invalid_argument const&) {}
        std::cout<<"FPS_NUI_ASSET_SMOKE_PASS\n";
        return 0;
    }catch(std::exception const& e) {
        std::cerr<<e.what()<<"\n";
        return 1;
    }
}
