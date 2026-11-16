#!/usr/bin/env python3
"""Mechanical migration of authored-player sites to component queries."""
from pathlib import Path
R=Path(__file__).resolve().parents[1]
def edit(name, pairs):
 p=R/name;s=p.read_text(encoding="utf-8-sig");original=s
 for old,new,n in pairs:
  count=s.count(old)
  if count==0 and new in s: continue
  if count!=n:raise RuntimeError(f"{name}: found {count}, expected {n}: {old[:100]!r}")
  s=s.replace(old,new)
 if s!=original:p.write_text(s,encoding="utf-8",newline="");print("updated",name)
n="engine/northstarcpp/modules/"
edit(n+"animation/animation.cpp",[
 ("#include <northstarcpp/scene_animation.hpp>","#include <northstarcpp/scene_animation.hpp>\n#include <northstarcpp/scene_pawn.hpp>",1),
 ("found->player || found->mass!=0.f","scene_pawn::is_pawn(scene,*found) || found->mass!=0.f",1),
])
edit(n+"host/fps_game.cpp",[
 ("#include <northstarcpp/fps_game.hpp>","#include <northstarcpp/fps_game.hpp>\n#include <northstarcpp/scene_pawn.hpp>",1),
 ("if (o.player) {","if (scene_pawn::is_local(scene,o)) {",1),
])
edit(n+"host/procedural_environment.cpp",[
 ('#include <northstarcpp/procedural_environment.hpp>','#include <northstarcpp/procedural_environment.hpp>\n#include <northstarcpp/scene_pawn.hpp>',1),
 ('    player.player=true;','',1),
 ('    result.objects.push_back(std::move(player));','    result.objects.push_back(std::move(player));\n    scene_pawn::migrate_imported_local(result,id);',1),
])
edit(n+"editor/editor_tools.cpp",[
 ('    copy.player = false;\n','',1),
])
edit(n+"editor/editor_prefab.cpp",[
 ('        object.player=false; // A prefab cannot create duplicate player controllers.\n','',1),
 ('        entity.player=false;\n','',1),
 ('''    for(auto const& component:original.components)
        if(included.count(component.entity_id))
            prefab.components.push_back(component);''',
  '''    for(auto const& component:original.components)
        if(included.count(component.entity_id) &&
           component.type!="northstar.gameplay.pawn")
            prefab.components.push_back(component);''',1),
])
edit(n+"host/scene_world.cpp",[
 ('#include <northstarcpp/scene_world.hpp>','#include <northstarcpp/scene_world.hpp>\n#include <northstarcpp/scene_pawn.hpp>',1),
 ('o.player || o.mass>0.f','scene_pawn::is_pawn(scene_,o) || o.mass>0.f',1),
 ('!moved || object.player || object.node.cell.empty()','!moved || scene_pawn::is_pawn(scene_,object) || object.node.cell.empty()',1),
 ('object.player || object.mass>0.f','scene_pawn::is_pawn(scene_,object) || object.mass>0.f',1),
 ('object.player || (node.visible &&','scene_pawn::is_pawn(scene_,object) || (node.visible &&',1),
])
edit(n+"editor/qt_editor_window.cpp",[
 ('    (void)player_character::register_schemas();','    (void)player_character::register_schemas();',1),
 ('actor.player?"player":procedural','player_character::has_pawn(scene_,actor.node.id)?"player":procedural',1),
 ('object.player?"Player":procedural','player_character::has_pawn(scene_,object.node.id)?"Player":procedural',1),
 ('outlinerGlyph(object.player?1:procedural','outlinerGlyph(player_character::has_pawn(scene_,object.node.id)?1:procedural',1),
])
edit(n+"editor/qt_editor_viewport.cpp",[
 ('if(object.player || authored_characters.count(object.node.id))','if(authored_characters.count(object.node.id))',1),
 ('|| actor.player)', '|| (scene_ && player_character::has_pawn(*scene_,actor.node.id)))',1),
])
edit(n+"viewport/viewport.cpp",[
 ('if ((fps || (camera_pose && selected==-2)) && object.player) continue;',
  'if ((fps || (camera_pose && selected==-2)) && scene_pawn::is_local(scene,object)) continue;',1),
 ('(selected==-2 && object.player)','(selected==-2 && scene_pawn::is_local(scene,object))',4),
 ('(object.player || character_entities.count(object.node.id))','character_entities.count(object.node.id)',1),
 ('#include <northstarcpp/player_character_components.hpp>',
  '#include <northstarcpp/player_character_components.hpp>\n#include <northstarcpp/scene_pawn.hpp>',1),
])
edit(n+"host/ecs_world.cpp",[
 ('#include <northstarcpp/scene_world.hpp>','#include <northstarcpp/scene_world.hpp>\n#include <northstarcpp/scene_pawn.hpp>',1),
 ('void apply_record(NsEcsApi const* api, void* world, NsEntity id, SceneObject const& object)\n{','void apply_record(NsEcsApi const* api, void* world, NsEntity id, SceneObject const& object,bool controlled)\n{',1),
 ('NsController controller{static_cast<uint8_t>(object.player)};',
  'NsController controller{static_cast<uint8_t>(controlled)};',1),
 ('    object.player = controller.enabled != 0;\n','',1),
 ('    read(NS_COMPONENT_CONTROLLER,&controller,sizeof(controller));','    read(NS_COMPONENT_CONTROLLER,&controller,sizeof(controller));\n    (void)controller; // runtime input is controlled by PlayerController',1),
 ('apply_record(api,replacement,id,object);','apply_record(api,replacement,id,object,scene_pawn::is_local(scene,object));',1),
 ('apply_record(api,impl_->world,id,created);','apply_record(api,impl_->world,id,created,scene_pawn::is_local(updated,created));',1),
 ('if (!object.node.id || object.player ||','if (!object.node.id ||',1),
 ('apply_record(api,impl_->world,id,object);','apply_record(api,impl_->world,id,object,false);',1),
 ('apply_record(impl_->api,impl_->world,id,changed);','apply_record(impl_->api,impl_->world,id,changed,scene_pawn::is_local(current,changed));',1),
 ('bool const wanted=obj.player || obj.mass>0.f || obj.node.cell.empty() ||',
  'bool const wanted=scene_pawn::is_pawn(source,obj) || obj.mass>0.f || obj.node.cell.empty() ||',1),
 ('apply_record(impl_->api,impl_->world,id,obj);',
  'apply_record(impl_->api,impl_->world,id,obj,scene_pawn::is_local(source,obj));',1),
])
print("Source migration complete")
