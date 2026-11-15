#pragma once
// D3D12 CPU-side HUD and primitive mesh generation; no device ownership.
#include <northstarcpp/render_plugin.h>
#include <vector>
namespace northstarcpp::render::d3d12::geometry {
struct Vertex {float x,y,z,r,g,b;};
void make_hud(const NorthStarRenderFrame& frame,unsigned width,unsigned height,
              std::vector<Vertex>& hud_triangles,std::vector<Vertex>& hud_lines);
void cube(std::vector<Vertex>& vertices,const NorthStarRenderCube& cube,bool selected);
}
