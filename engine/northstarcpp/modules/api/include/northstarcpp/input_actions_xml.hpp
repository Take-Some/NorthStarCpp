// NorthStar InputActionMap XML codec. Project assets: assets/input/actions.xml.
// Explicitly versioned; bounded, validated and independent from runtime providers.
#pragma once
#include <northstarcpp/input_actions.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>
#include <sstream>
#include <stdexcept>
#include <string>

namespace northstarcpp::gameplay {
inline std::string serialize_input_actions(InputActionMap const& map) {
    boost::property_tree::ptree doc,root;
    root.put("<xmlattr>.version",1);
    for(auto const& context:map.contexts()) {
        boost::property_tree::ptree item;
        item.put("<xmlattr>.name",context.name);
        item.put("<xmlattr>.priority",context.priority);
        item.put("<xmlattr>.enabled",context.enabled?1:0);
        item.put("<xmlattr>.consume",context.consumes_keys?1:0);
        for(auto const& binding:context.bindings) {
            boost::property_tree::ptree input;
            input.put("<xmlattr>.action",binding.action);
            input.put("<xmlattr>.key",unsigned(binding.key));
            item.add_child("Binding",input);
        }
        root.add_child("Context",item);
    }
    doc.add_child("InputActions",root);
    std::ostringstream stream;
    // Both bundled and system Boost versions accept the default writer.
    boost::property_tree::write_xml(stream,doc);
    return stream.str();
}
inline InputActionMap parse_input_actions(std::string const& xml) {
    if(xml.empty() || xml.size()>65536)
        throw std::invalid_argument("Input action XML must be 1..65536 bytes");
    boost::property_tree::ptree doc;
    std::istringstream stream(xml);
    boost::property_tree::read_xml(stream,doc,
        boost::property_tree::xml_parser::no_comments |
        boost::property_tree::xml_parser::trim_whitespace);
    auto const& root=doc.get_child("InputActions");
    if(root.get<int>("<xmlattr>.version")!=1)
        throw std::invalid_argument("Unsupported InputActions XML version");
    InputActionMap map(false);
    std::size_t count=0;
    for(auto const& [kind,section]:root) {
        if(kind=="<xmlattr>" || kind=="<xmltext>")continue;
        if(kind!="Context")throw std::invalid_argument("Unknown InputActions element");
        const auto name=section.get<std::string>("<xmlattr>.name");
        const auto priority=section.get<int>("<xmlattr>.priority");
        const auto enabled=section.get<int>("<xmlattr>.enabled");
        const auto consume=section.get<int>("<xmlattr>.consume");
        if((enabled!=0 && enabled!=1)||(consume!=0 && consume!=1))
            throw std::invalid_argument("Invalid input context flags");
        map.add_context(name,priority,enabled!=0,consume!=0);
        ++count;
        for(auto const& [tag,input]:section) {
            if(tag=="<xmlattr>" || tag=="<xmltext>")continue;
            if(tag!="Binding")throw std::invalid_argument("Unknown input binding element");
            const auto action=input.get<std::string>("<xmlattr>.action");
            const auto key=input.get<unsigned>("<xmlattr>.key");
            map.bind(name,action,key);
        }
    }
    if(!count)throw std::invalid_argument("InputActions XML contains no contexts");
    return map;
}
} // namespace northstarcpp::gameplay
