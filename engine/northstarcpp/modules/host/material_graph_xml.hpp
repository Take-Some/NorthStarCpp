// Typed XML serialization of Material Graph ASTs.
// JSON exists exclusively as a compiler-facing in-memory representation.
#pragma once
#include <boost/property_tree/ptree.hpp>
#include <string>
#include <cstddef>
namespace northstarcpp::shared_assets::detail {
boost::property_tree::ptree typed_xml_value_from_json(
    std::string const& source);
boost::property_tree::ptree graph_xml_value_from_json(
    std::string const& source);
std::string typed_material_graph_from_json_library(
    std::string const& source,std::size_t material_index);
std::string graph_json_from_xml_value(
    boost::property_tree::ptree const& node);
}
