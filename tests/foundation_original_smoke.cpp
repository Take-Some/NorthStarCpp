// Test calls the ORIGINAL foundation classes/functions compiled by northstarcpp_math.
// No compatibility shim, duplicate primitive generator, or exported wrapper.
#include <northstarcpp/core/math_standalone.h>
#include <northstarcpp/geometry_utils.h>
#include <northstarcpp/strings_functions.h>
#include <northstarcpp/buffer_string.h>
#include <northstarcpp/fs_path.h>
#include <cstring>
#include <stdexcept>

int main() {
    using namespace northstarcpp;
    if(!strings::compare_with_wildcards("engine_*","engine_scene"))
        throw std::runtime_error("Original strings wildcard comparison not linked");
    if(strings::compare_insensitive("TeSt","test")!=0)
        throw std::runtime_error("Original case-insensitive comparison failed");
    float parsed=0.f;
    if(!strings::convert_string_to_number("12.5",&parsed) || parsed!=12.5f)
        throw std::runtime_error("Original float parser not linked");

    char chars[128]{};
    buffer_string text(chars,128);
    text += "original compatibility";
    text.make_uppercase();
    if(std::strcmp(text.c_str(),"ORIGINAL COMPATIBILITY")!=0)
        throw std::runtime_error("Original buffer_string is not functional");
    text.make_lowercase();
    if(std::strcmp(text.c_str(),"original compatibility")!=0)
        throw std::runtime_error("Original buffer_string lowercase failed");
    auto const portable=fs::convert_to_portable("C:\\Data\\NorthStar\\main.nscn");
    if(std::strcmp(portable.c_str(),"C:/Data/NorthStar/main.nscn")!=0)
        throw std::runtime_error("Original portable path conversion unavailable");
    auto const native=fs::convert_to_native("C:/Data/NorthStar/main.nscn");
    if(std::strcmp(native.c_str(),"C:/Data/NorthStar/main.nscn")!=0)
        throw std::runtime_error("Original native path conversion unavailable");

    using namespace geometry_utils;
    geom_vertices_type vertices;
    geom_indices_type indices;
    math::float4x4 transform;
    transform.identity();
    if(!create_cube(vertices,indices,transform,math::float3(1,2,3))
       || vertices.size()!=8 || indices.size()!=36)
        throw std::runtime_error("Original cube implementation not linked");
    if(!create_cylinder(vertices,indices,transform,math::float3(1,2,3))
       || vertices.empty() || indices.empty())
        throw std::runtime_error("Original cylinder implementation unavailable");
    if(!create_ellipsoid(vertices,indices,transform,math::float3(1,2,3))
       || vertices.empty() || indices.empty())
        throw std::runtime_error("Original ellipsoid implementation unavailable");
    if(!create_ring(vertices,indices,1.f,0.25f,12)
       || vertices.size()!=24 || indices.size()!=72)
        throw std::runtime_error("Original ring implementation unavailable");
    if(!create_torus(vertices,indices,transform,2.f,0.5f,12,12)
       || vertices.size()!=144 || indices.size()!=864)
        throw std::runtime_error("Original torus implementation unavailable");
    return 0;
}
