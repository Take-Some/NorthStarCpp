// Authored portal polygon geometry stored in NEF8 SceneComponentBlob XML.
// Source `.nscn` format remains unchanged: no parallel scene format.
#pragma once
#include <northstarcpp/scene.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>
#include <algorithm>
#include <cmath>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
namespace northstarcpp::scene_portal_polygon {
struct Polygon {
    std::string name;
    std::vector<Vector3> vertices;
};
inline Polygon parse(std::string const& xml) {
    if(xml.empty() || xml.size()>65536)
        throw std::invalid_argument("portal polygon XML exceeds bound");
    boost::property_tree::ptree doc;
    std::istringstream stream(xml);
    boost::property_tree::read_xml(stream,doc,
        boost::property_tree::xml_parser::trim_whitespace);
    auto const& root=doc.get_child("NorthStarPortalPolygon");
    if(root.get<int>("<xmlattr>.version",1)!=1)
        throw std::invalid_argument("unsupported polygon component version");
    Polygon polygon;
    polygon.name=root.get<std::string>("<xmlattr>.name","");
    if(polygon.name.empty() || polygon.name.size()>128)
        throw std::invalid_argument("portal polygon must name a portal");
    for(auto const& element:root) {
        if(element.first!="Vertex")continue;
        if(polygon.vertices.size()>=64)
            throw std::invalid_argument("portal has more than 64 vertices");
        auto const& item=element.second;
        Vector3 const point{item.get<float>("<xmlattr>.x"),
                            item.get<float>("<xmlattr>.y"),
                            item.get<float>("<xmlattr>.z")};
        if(!std::isfinite(point.x)||!std::isfinite(point.y)||
           !std::isfinite(point.z))
            throw std::invalid_argument("non-finite portal vertex");
        polygon.vertices.push_back(point);
    }
    if(polygon.vertices.size()<3)
        throw std::invalid_argument("portal requires >=3 vertices");
    auto const sub=[](Vector3 a,Vector3 b){
        return Vector3{a.x-b.x,a.y-b.y,a.z-b.z};
    };
    auto const cross=[](Vector3 a,Vector3 b) {
        return Vector3{a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,
                       a.x*b.y-a.y*b.x};
    };
    auto const dot=[](Vector3 a,Vector3 b) {
        return double(a.x)*b.x+double(a.y)*b.y+double(a.z)*b.z;
    };
    auto const anchor=polygon.vertices[0];
    Vector3 normal{};
    for(std::size_t i=1;i+1<polygon.vertices.size();++i) {
        normal=cross(sub(polygon.vertices[i],anchor),
                     sub(polygon.vertices[i+1],anchor));
        if(dot(normal,normal)>1.e-10)break;
    }
    auto const magnitude=std::sqrt(dot(normal,normal));
    if(magnitude<1.e-5)
        throw std::invalid_argument("degenerate portal polygon");
    normal={float(normal.x/magnitude),float(normal.y/magnitude),
            float(normal.z/magnitude)};
    for(auto const& v:polygon.vertices)
        if(std::abs(dot(normal,sub(v,anchor)))>0.01)
            throw std::invalid_argument("portal polygon is nonplanar");
    // Concave polygons are rejected rather than incorrectly culling rooms;
    // a decomposition/polygon clipping strategy is required for that case.
    for(std::size_t i=0;i<polygon.vertices.size();++i) {
        auto const a=polygon.vertices[i];
        auto const b=polygon.vertices[(i+1)%polygon.vertices.size()];
        auto const c=polygon.vertices[(i+2)%polygon.vertices.size()];
        auto const winding=dot(normal,cross(sub(b,a),sub(c,b)));
        if(winding < -1.e-5)
            throw std::invalid_argument("portal polygon is not convex");
    }
    return polygon;
}
}
