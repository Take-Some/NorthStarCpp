#!/usr/bin/env python3
"""Clip Vulkan's fallback editor grid to a finite, fading ground footprint."""
from pathlib import Path
from datetime import datetime
import sys
root=Path(__file__).resolve().parents[2]
files=[root/"engine/northstarcpp/modules/viewport/viewport_draw.cpp",
       root/"engine/northstarcpp/modules/viewport/viewport_draw.inc"]
patch='''    // D3D11/D3D12 retain the analytic GPU grid; Vulkan's temporary
    // geometric fallback must never extend its strips to the horizon.
    if(gizmo && gizmo->show_grid && camera_pose && !fps &&
       !state->editor_grid_api) {
        auto const layout=editor_grid::build(
            *camera_pose,gizmo->grid_width,gizmo->grid_height);
        auto const minor=std::clamp(layout.minor_step*.022f,.009f,20.f);
        const bool vk=state->module->provider()=="vulkan";
        const float radius=std::clamp(camera_pose->far_plane*.24f,30.f,190.f);
        for(auto const& line:layout.lines) {
            if(cubes.size()>=max_scene_objects)break;
            if(vk) {
                const bool x_axis=std::abs(line.to.x-line.from.x)>.01f;
                const float fixed=x_axis?line.from.z:line.from.x;
                const float camera_fixed=x_axis?
                    camera_pose->position.z:camera_pose->position.x;
                const float perp=fixed-camera_fixed;
                if(std::abs(perp)>=radius)continue;
                const float half=std::sqrt(radius*radius-perp*perp);
                const float center=x_axis?
                    camera_pose->position.x:camera_pose->position.z;
                const float a=x_axis?line.from.x:line.from.z;
                const float b=x_axis?line.to.x:line.to.z;
                const float lo=std::max(std::min(a,b),center-half);
                const float hi=std::min(std::max(a,b),center+half);
                if(hi-lo<.01f)continue;
                const int segments=std::clamp(
                    int(std::ceil((hi-lo)/24.f)),1,16);
                const float thickness=line.origin?minor*2.f:
                    line.major?minor*1.35f:minor;
                for(int i=0;i<segments &&
                    cubes.size()<max_scene_objects;++i) {
                    const float a0=lo+(hi-lo)*float(i)/segments;
                    const float a1=lo+(hi-lo)*float(i+1)/segments;
                    const float mid=(a0+a1)*.5f;
                    const float dist=std::hypot(perp,mid-center);
                    const float fade=std::clamp(
                        (radius-dist)/(radius*.35f),0.f,1.f);
                    if(fade<.07f)continue;
                    auto color=[&](int shift) {
                        const float value=float((line.color>>shift)&255u);
                        const float bg=float((0x18283bu>>shift)&255u);
                        return std::uint32_t(std::clamp(
                            bg+(value-bg)*fade,0.f,255.f));
                    };
                    NorthStarRenderCube strip{};
                    strip.size=1.f;
                    strip.rgb=(color(16)<<16)|(color(8)<<8)|color(0);
                    strip.x=x_axis?mid:fixed;
                    strip.z=x_axis?fixed:mid;
                    strip.y=.018f-.5f;
                    strip.rotation[0]=x_axis?a1-a0:thickness;
                    strip.rotation[4]=std::max(.008f,minor*.25f);
                    strip.rotation[8]=x_axis?thickness:a1-a0;
                    cubes.push_back(strip);
                }
                continue;
            }
            // Legacy external providers without analytic grid ABI.
            NorthStarRenderCube strip{};
            strip.size=1.f;
            strip.rgb=line.color;
            strip.x=(line.from.x+line.to.x)*.5f;
            strip.y=.012f-.5f;
            strip.z=(line.from.z+line.to.z)*.5f;
            strip.rotation[0]=std::max(minor*.75f,
                std::abs(line.to.x-line.from.x));
            strip.rotation[4]=std::max(.008f,minor*.25f);
            strip.rotation[8]=std::max(minor*.75f,
                std::abs(line.to.z-line.from.z));
            auto const thickness=line.origin?minor*2.f:
                line.major?minor*1.35f:minor;
            if(std::abs(line.to.x-line.from.x)>.01f)
                strip.rotation[8]=thickness;
            else strip.rotation[0]=thickness;
            cubes.push_back(strip);
        }
    }

'''
updates={}
for p in files:
 s=p.read_text(encoding="utf8").replace("\r\n","\n")
 a=s.index("    // Authoring-only adaptive ground grid")
 b=s.index("    if(gizmo && gizmo->show_collisions",a)
 updates[p]=s[:a]+patch+s[b:]
print("VALIDATED",len(updates))
if "--apply" in sys.argv:
 stamp=datetime.now().strftime("%Y%m%d-%H%M%S")
 for p,s in updates.items():
  data=p.read_bytes()
  p.with_name(p.name+".before-grid-"+stamp+".bak").write_bytes(data)
  p.write_bytes((s.replace("\n","\r\n") if b"\r\n" in data else s).encode("utf8"))
  print("APPLIED",p)
