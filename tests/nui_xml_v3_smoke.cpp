#include "../engine/northstarcpp/modules/shared/ui_xml_codec.hpp"
#include <boost/property_tree/json_parser.hpp>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <stdexcept>

using northstarcpp::nui_xml::compile;
int main(int argc,char** argv) {
    try {
        if(argc<2)throw std::runtime_error("Specify one or more .nui files");
        int total=0;
        for(int i=1;i<argc;++i){
            std::ifstream in(argv[i],std::ios::binary);
            if(!in)throw std::runtime_error("UI asset missing: "+std::string(argv[i]));
            auto bytes=std::string(std::istreambuf_iterator<char>(in),
                                   std::istreambuf_iterator<char>());
            if(bytes.find("<NorthStarUI")==std::string::npos)
                throw std::runtime_error("UI file does not contain XML");
            auto normalized=compile(bytes);
            boost::property_tree::ptree document;
            std::istringstream json(normalized);
            boost::property_tree::read_json(json,document);
            if(document.get<std::string>("schema")!="northstar.ui/2" ||
               !document.get_child_optional("nodes"))
                throw std::runtime_error("Broken UI normalized document");
            ++total;
        }
        const auto sample=compile(R"(<?xml version="1.0"?>
<NorthStarUI version="3" target="screen">
<Tokens><Token name="accent" type="color" value="#aabbccff"/></Tokens>
<Styles><Style name="caption"><Property name="color" token="accent"/></Style></Styles>
<Elements><Text id="label" style="caption">
<Property name="text" type="string" value="0012"/>
<Property name="visible" type="bool" value="true"/>
<Property name="number" type="number" value="12"/>
</Text></Elements></NorthStarUI>)");
        if(sample.find("\"text\":\"0012\"")==std::string::npos ||
           sample.find("\"visible\":true")==std::string::npos ||
           sample.find("\"number\":12")==std::string::npos)
            throw std::runtime_error("XML value types were erased");
        try {
            compile("<!DOCTYPE foo><NorthStarUI version=\"3\" target=\"screen\"><Elements/></NorthStarUI>");
            throw std::runtime_error("A DTD was accepted");
        }catch(std::invalid_argument const&){}
        try {
            compile("<NorthStarUI version=\"3\" target=\"screen\"><Elements>"
                "<Button id=\"broken\" parent=\"unknown\"/></Elements></NorthStarUI>");
            throw std::runtime_error("Invalid element parent accepted");
        }catch(std::invalid_argument const&){}
        std::cout<<"NUI_XML_V3_SMOKE_PASS "<<total<<" XML assets\n";
    }catch(std::exception const& e){
        std::cerr<<"NUI_XML_V3_SMOKE_FAIL: "<<e.what()<<"\n";
        return 1;
    }
}
