#!/usr/bin/env python3
"""One-time source refactor: remove the serialized SceneObject::player field.

Strict matches prevent accidental broad rewrites in concurrently edited files.
Run only on an isolated working tree; no old files are deleted.
"""
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
def edit(relative, replacements):
    path=ROOT/relative
    content=path.read_text(encoding="utf-8-sig")
    original=content
    for old,new,expected in replacements:
        matches=content.count(old)
        if matches!=expected:
            raise RuntimeError(f"{relative}: expected {expected} matches for {old[:65]!r}, found {matches}")
        content=content.replace(old,new)
    if content!=original:
        path.write_text(content,encoding="utf-8",newline="")
        print("updated",relative)

edit("engine/northstarcpp/modules/api/include/northstarcpp/scene.hpp",[
    ("    bool player = false;\n","",1),
])
edit("engine/northstarcpp/modules/host/scene.cpp",[
    ('#include <northstarcpp/scene_component_registry.hpp>',
     '#include <northstarcpp/scene_component_registry.hpp>\n#include <northstarcpp/scene_pawn.hpp>',1),
    ('''    return {{
        {"Player", {0.f, 0.f, 0.f}, 1.5f, 0x46b9d8, true, 2.f},
        {"Static Block", {0.f, 0.f, 3.f}, 1.5f, 0xf2a65a, false, 0.f},
        {"Pushable Cube", {2.4f, 0.f, 0.f}, 1.f, 0x9bd783, false, 1.f},
        {"Falling Cube", {-2.4f, 4.f, 2.f}, 1.f, 0xb486f0, false, 1.f},
    }};''',
     '''    Scene scene;
    scene.version=3;
    scene.objects={
        {"Player", {0.f, 0.f, 0.f}, 1.5f, 0x46b9d8, 2.f},
        {"Static Block", {0.f, 0.f, 3.f}, 1.5f, 0xf2a65a, 0.f},
        {"Pushable Cube", {2.4f, 0.f, 0.f}, 1.f, 0x9bd783, 1.f},
        {"Falling Cube", {-2.4f, 4.f, 2.f}, 1.f, 0xb486f0, 1.f},
    };
    for(std::size_t i=0;i<scene.objects.size();++i)
        scene.objects[i].node.id=i+1;
    scene_pawn::migrate_imported_local(scene,scene.objects[0].node.id);
    return scene;''',1),
    ('    bool has_player = false;\n','',1),
    ('''        if (object.player && (has_player || object.mass == 0.f))
            throw std::runtime_error("The scene can have one player-controlled cube with positive mass");
        has_player |= object.player;
''','',1),
    ('''        object.player = player == 1;
        if (legacy && object.player)
            object.mass = 1.f;
        scene.objects.push_back(std::move(object));
''',
    '''        if (legacy && player == 1)
            object.mass = 1.f;
        object.node.id=scene.objects.size()+1;
        scene.objects.push_back(std::move(object));
        if(player==1) {
            scene.version=3;
            scene_pawn::migrate_imported_local(scene,scene.objects.back().node.id);
        }
''',1),
    ('''    validate_scene(scene);
    return scene;
}

void save_scene''',
    '''    // All legacy files are imported into component-based V3 scenes.
    // Their old integer marker is not retained in SceneObject.
    scene.version=3;
    validate_scene(scene);
    return scene;
}

void save_scene''',1),
])
print("core migration source prepared")
