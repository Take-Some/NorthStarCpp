from pathlib import Path
R=Path(__file__).resolve().parents[1]
def edit(file, pairs):
 p=R/file;s=p.read_text(encoding="utf-8-sig")
 for a,b,n in pairs:
  c=s.count(a)
  if c!=n:raise RuntimeError(f"{file}: got {c}/{n} {a[:80]!r}")
  s=s.replace(a,b)
 p.write_text(s,encoding="utf-8",newline="")
 print("updated",file)
n="engine/northstarcpp/modules/"
edit(n+"api/include/northstarcpp/player_controller.hpp",[
 ('        bool const legacy=scene.objects[*index].player;\n        if(!legacy && !player_character::has_pawn(scene,id))',
  '        if(!player_character::has_pawn(scene,id))',1),
 ('''        if(!candidate)
            for(auto const& object:scene.objects)
                if(object.player && object.node.id) {
                    candidate=object.node.id;
                    break;
                }
''','',1),
 ('// Prefer an explicit auto-possess component over the legacy player flag.','// Auto-possess is component metadata; there is no SceneObject fallback.',1),
])
edit(n+"editor/qt_editor_window_setup.cpp",[
 ('#include <northstarcpp/player_character_components.hpp>',
  '#include <northstarcpp/player_character_components.hpp>\n#include <northstarcpp/scene_pawn.hpp>',1),
 ('hasPlayer |= actor.player;','hasPlayer |= scene_pawn::is_local(scene,actor);',1),
 ('            character.player=!hasPlayer; // only one legacy controlled Pawn\n','',1),
 ('if(scene_.objects[i].player) {','if(scene_pawn::is_local(scene_,scene_.objects[i])) {',1),
 ('                scene,actor.node.id,actor.player);',
  '                scene,actor.node.id,!scene_pawn::local_index(scene).has_value() ||\n                    scene_pawn::is_local(scene,actor));',1),
])
edit(n+"viewport/viewport.cpp",[
 ('} else if (selected == -2 && object.player) {',
  '} else if (selected == -2 && scene_pawn::is_local(scene,object)) {',1),
])
print("No authored flag referenced by controller/editor/GPU")
