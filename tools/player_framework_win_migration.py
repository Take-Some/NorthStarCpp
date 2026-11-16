from pathlib import Path
R=Path(__file__).resolve().parents[1]
p=R/"engine/northstarcpp/modules/editor/window.cpp"
s=p.read_text(encoding="utf-8-sig")
for old,new,n in [
 ('#include <northstarcpp/physics.hpp>','#include <northstarcpp/physics.hpp>\n#include <northstarcpp/scene_pawn.hpp>',1),
 ('                auto const& objects=document.snapshot().objects;\n                scripted_game=std::any_of(objects.begin(),objects.end(),\n                    [](SceneObject const& object) {return object.player || !object.node.script.empty();});',
  '''                auto const source=document.snapshot();
                scripted_game=std::any_of(source.objects.begin(),source.objects.end(),
                    [&](SceneObject const& object) {
                        return scene_pawn::is_local(source,object) ||
                               !object.node.script.empty();
                    });''',1),
 ('if (object.player) {','if (scene_pawn::is_local(scene,object)) {',2),
 ('if (object.player) {','if (scene_pawn::is_local(preview->scene(),object)) {',1),
 ('''        auto it=std::find_if(scene.objects.begin(),scene.objects.end(),
            [](SceneObject const& obj) {return obj.player;});
        if (it==scene.objects.end()) return;
        auto index=static_cast<std::size_t>(it-scene.objects.begin());''',
  '''        auto controlled=preview->controlled_index();
        if(!controlled || *controlled>=scene.objects.size())return;
        auto const& actor=scene.objects[*controlled];
        auto index=*controlled;''',1),
 ('target.eye=it->position;','target.eye=actor.position;',1),
 ('if (view.objects[i].player)name+=L"  [Player]";',
  'if (scene_pawn::is_local(view,view.objects[i]))name+=L"  [Local Pawn]";',1),
 ('(is_basic ? object.player : node.visible) ? BST_CHECKED : BST_UNCHECKED,0);',
  '(is_basic ? scene_pawn::is_local(scene,object) : node.visible) ? BST_CHECKED : BST_UNCHECKED,0);',1),
 ('        if(inspector_page!=0)\n            editor_tools::promote_to_v3(changed);',
  '        editor_tools::promote_to_v3(changed);',1),
 ('''            object.player=SendMessageW(player,BM_GETCHECK,0,0)==BST_CHECKED;
            if(object.player) {
                if(object.mass==0.f) object.mass=1.f;
                for(std::size_t i=0;i<changed.objects.size();++i)
                    if(i!=index)changed.objects[i].player=false;
            }''',
  '''            bool const possess=SendMessageW(player,BM_GETCHECK,0,0)==BST_CHECKED;
            if(possess) {
                if(object.mass==0.f)object.mass=1.f;
                player_character::add_character_components(changed,object.node.id,false);
            }
            for(auto& component:changed.components) {
                if(component.type!=player_character::pawn_type ||
                   component.version!=player_character::version ||
                   component.payload.size()!=12)continue;
                // Change only auto-possess metadata, never destroy Pawn/Camera.
                component.payload[4]=char(possess &&
                    component.entity_id==object.node.id?1:0);
            }''',1),
 ('if (obj.player || !obj.node.script.empty() ||',
  'if (scene_pawn::is_pawn(prefab,obj) || !obj.node.script.empty() ||',1),
 ('''            auto player=std::find_if(objects.begin(),objects.end(),
                [](SceneObject const& o){return o.player;});
            if(player==objects.end())''',
  '''            auto idx=preview->controlled_index();
            if(!idx || *idx>=objects.size())''',1),
 ('            auto it=std::find_if(objects.begin(),objects.end(),\n                [](SceneObject const& obj){return obj.player;});\n            if (it==objects.end()) throw std::runtime_error("No player in scene");',
  '''            auto idx=preview->controlled_index();
            if(!idx || *idx>=objects.size())
                throw std::runtime_error("No possessed Pawn in scene");
            auto it=objects.begin()+static_cast<std::ptrdiff_t>(*idx);''',1),
 ('(obj.player?"true":"false")',
  '(scene_pawn::is_local(preview->scene(),obj)?"true":"false")',1),
 ('objs[i].mass!=0.f || objs[i].player',
  'objs[i].mass!=0.f || scene_pawn::is_pawn(preview->scene(),objs[i])',1),
 ('return obj.player ? std::string("true") : std::string("false");',
  'return preview->controlled_entity()==obj.node.id ? std::string("true") : std::string("false");',1),
 ('body.mass!=0.f || body.player',
  'body.mass!=0.f || scene_pawn::is_pawn(preview->scene(),body)',1),
 ('obj.mass!=0.f || obj.player',
  'obj.mass!=0.f || scene_pawn::is_pawn(preview->scene(),obj)',1),
 ('if(body.mass>0.f && !body.player)',
  'if(body.mass>0.f && !scene_pawn::is_pawn(preview->scene(),body))',1),
 ('!document.object(document.at(0)).player)',
  '!scene_pawn::is_local(document.snapshot(),document.object(document.at(0))))',1),
 ('auto const movable = [](SceneObject const& object) { return object.player; };',
  'auto const movable = [this](SceneObject const& object) {return scene_pawn::is_local(preview->scene(),object);};',1),
 ('L"Player-controlled"','L"Auto Possess Local"',1),
]:
 count=s.count(old)
 if count!=n:raise RuntimeError(f"{count}/{n} for {old[:100]!r}")
 s=s.replace(old,new,1)
p.write_text(s,encoding="utf-8",newline="")
print("Win32 Editor/Player references migrated")
