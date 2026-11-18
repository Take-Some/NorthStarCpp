#include <northstarcpp/scene_world.hpp>
#include <northstarcpp/scene_camera_frustum.hpp>
#include <stdexcept>
using namespace northstarcpp;
void ck(bool v,char const* m){if(!v)throw std::runtime_error(m);}
int main(){
 Scene s;s.version=3;s.name="Polygon Portal Test";
 SceneObject a,b,c;
 a.node.id=1;a.node.room="A";
 b.node.id=2;b.node.room="B";b.position={0,0,15};
 c.node.id=3;c.node.room="C";c.position={8,0,24};
 s.objects={a,b,c};
 s.portals={{"AB","A","B",{-9,-2,9.9f},{9,2,10.1f},true},
            {"BC","B","C",{7,-1,19.9f},{9,1,20.1f},true}};
 SceneComponentBlob p;
 p.type="engine.scene.portal_polygon";
 p.payload=R"(<NorthStarPortalPolygon version="1" name="AB">
 <Vertex x="-0.5" y="-1" z="10"/><Vertex x="0.5" y="-1" z="10"/>
 <Vertex x="0.5" y="1" z="10"/><Vertex x="-0.5" y="1" z="10"/>
 </NorthStarPortalPolygon>)";
 auto const f=make_scene_view_frustum(CameraPose{},16.f/9.f);
 s.components={p};
 SceneWorld narrow(s);narrow.set_view_frustum(f);narrow.update({0,0,0},{},"A");
 ck(narrow.object_visible(0)&&narrow.object_visible(1)&&
    !narrow.object_visible(2),"narrow polygon leaked the C room");
 auto const bytes=serialize_scene_binary(s);
 ck(parse_scene_binary(bytes).components.front().payload==p.payload,
    "NEF8 lost authored polygon");
 s.components.clear();
 SceneWorld broad(s);broad.set_view_frustum(f);broad.update({0,0,0},{},"A");
 ck(broad.object_visible(2),"AABB fallback clipped a visible portal");
 s.components={p};s.components[0].payload="<malformed";
 SceneWorld malformed(s);
 malformed.set_view_frustum(f);malformed.update({0,0,0},{},"A");
 ck(malformed.object_visible(2),"malformed optional polygon must fail open");
 return 0;
}
