#!/usr/bin/env python3
"""Keep original YDD sky mesh UVs and clip its triangles at Vulkan near plane.
The procedural GPU atmosphere remains a fullscreen shader background.
"""
from pathlib import Path
import datetime,sys
root=Path(__file__).resolve().parents[2]
p=root/"engine/northstarcpp/modules/render/vulkan/renderer_materials.cpp"
source=p.read_text(encoding="utf8").replace("\r\n","\n")
start=source.index("        if(sky) {\n            bool const gpu_atmosphere")
end=source.index("        for(std::size_t i=0;i+2<batch.vertices.size();i+=3) {",start)
block=source[start:end]
new=block.replace("if(sky) {\n            bool const gpu_atmosphere=gpu_sky_uniforms_.flags[0]>.5f;",
                  "if(sky && gpu_sky_uniforms_.flags[0]>.5f) {")
uv_start=new.index("                if(gpu_atmosphere) {")
uv_end=new.index("                // Sky-only interpolation channel",uv_start)
new=new[:uv_start]+'''                out.uv[0]=xy[0]*.5f+.5f;
                out.uv[1]=xy[1]*.5f+.5f;
'''+new[uv_end:]
assert new!=block and "gpu_atmosphere" not in new
source=source[:start]+new+source[end:]
old='''                visible=visible&&distance>near_clip;
                auto& out=tri[j];'''
new='''                // Authored sky triangles that cross the near plane are
                // clipped below. Rejecting the complete triangle produced
                // holes in custom skydomes when rotating the editor camera.
                if(!sky)visible=visible&&distance>near_clip;
                auto& out=tri[j];'''
assert source.count(old)==1
source=source.replace(old,new)
old='''            if(visible)
                material_frame_vertices_.insert(material_frame_vertices_.end(),
                                                 tri,tri+3);
        }
        draw.count='''
new='''            if(sky) {
                // Source YDD UVs and geometry must remain intact. Clip the
                // view-space polygon against Vulkan's positive near plane,
                // interpolating its attributes before perspective division.
                // The far depth is constant, so sky never occludes geometry.
                auto interpolate=[](MaterialClipVertex const& a,
                                    MaterialClipVertex const& b,float t) {
                    MaterialClipVertex out{};
                    for(int k=0;k<4;++k) {
                        out.clip[k]=a.clip[k]+(b.clip[k]-a.clip[k])*t;
                        out.tint[k]=a.tint[k]+(b.tint[k]-a.tint[k])*t;
                        out.params[k]=a.params[k]+(b.params[k]-a.params[k])*t;
                    }
                    for(int k=0;k<2;++k)
                        out.uv[k]=a.uv[k]+(b.uv[k]-a.uv[k])*t;
                    for(int k=0;k<3;++k) {
                        out.normal[k]=a.normal[k]+
                            (b.normal[k]-a.normal[k])*t;
                        out.world[k]=a.world[k]+(b.world[k]-a.world[k])*t;
                    }
                    return out;
                };
                std::array<MaterialClipVertex,5> clipped{};
                unsigned count=0;
                constexpr unsigned vertices=3;
                for(unsigned j=0;j<vertices;++j) {
                    auto const& a=tri[(j+vertices-1)%vertices];
                    auto const& b=tri[j];
                    bool a_inside=a.clip[3]>near_clip*1.001f;
                    bool b_inside=b.clip[3]>near_clip*1.001f;
                    if(a_inside!=b_inside) {
                        float t=(near_clip*1.001f-a.clip[3])/
                            (b.clip[3]-a.clip[3]);
                        clipped[count++]=interpolate(a,b,std::clamp(t,0.f,1.f));
                    }
                    if(b_inside)clipped[count++]=b;
                }
                for(unsigned j=1;j+1<count;++j) {
                    material_frame_vertices_.push_back(clipped[0]);
                    material_frame_vertices_.push_back(clipped[j]);
                    material_frame_vertices_.push_back(clipped[j+1]);
                }
            } else if(visible)
                material_frame_vertices_.insert(material_frame_vertices_.end(),
                                                 tri,tri+3);
        }
        draw.count='''
assert source.count(old)==1
source=source.replace(old,new)
print("Validated Vulkan authored sky near-plane clipping source")
if "--apply" in sys.argv:
 stamp=datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
 backup=p.with_name(p.name+".before-native-sky-clip-"+stamp+".bak")
 original=p.read_bytes()
 backup.write_bytes(original)
 p.write_bytes((source.replace("\n","\r\n") if b"\r\n" in original else source).encode("utf8"))
 print("PATCHED",p, "backup",backup.name)
