// NorthStarCpp editor authoring regression
#include "editor_tools.hpp"
#include <northstarcpp/ecs.hpp>

#include <stdexcept>
#include <cmath>
#include <limits>
#include <string>

int main()
{
    using namespace northstarcpp;
    using namespace northstarcpp::editor_tools;

    Scene scene;
    scene.objects.push_back(SceneObject{});
    scene.objects[0].name="Root";
    scene.objects[0].mass=1.f;
    validate_scene(scene);
    promote_to_v3(scene);
    if (scene.version!=3 || scene.objects[0].node.id==0)
        return 1;

    auto child=duplicate(scene,0,true);
    if (scene.objects[child].node.parent != scene.objects[0].node.id ||
        scene.objects[child].player)
        return 2;
    auto sibling=duplicate(scene,0);
    if (scene.objects[sibling].node.parent!=0 ||
        scene.objects[sibling].node.id==scene.objects[0].node.id)
        return 3;

    auto const layer=create_layer(scene,child);
    auto const cell=create_cell(scene,child);
    create_volume(scene,child);
    create_portal(scene,child);
    if (scene.layers[layer].name!=scene.objects[child].node.layer ||
        scene.cells[cell].name!=scene.objects[child].node.cell ||
        scene.volumes.size()!=1 || scene.portals.size()!=1)
        return 4;
    if (hierarchy_label(scene,child).find("  ")!=0)
        return 5;

    auto const serialized=serialize_scene_v3(scene);
    auto const restored=parse_scene_v3(serialized);
    if (restored.objects.size()!=3 ||
        restored.objects[child].node.parent!=restored.objects[0].node.id ||
        restored.cells.size()!=1 || restored.volumes.size()!=1 ||
        restored.portals.size()!=1)
        return 6;

    EcsWorld ecs;
    ecs.load(restored);
    auto const snapshot=ecs.snapshot();
    if (snapshot.objects[child].node.layer!=scene.objects[child].node.layer)
        return 7;

    History history;
    history.record(scene,1);
    auto changed=scene;
    auto count=delete_subtree(changed,0);
    if (changed.objects.size()!=1 || changed.objects[0].name!="Root Copy" || count!=0)
        return 8;
    Scene undo; std::size_t selection=0;
    if (!history.undo(changed,0,undo,selection) ||
        undo.objects.size()!=3 || selection!=1 || !history.can_redo())
        return 9;
    Scene redo; std::size_t next=99;
    if (!history.redo(undo,1,redo,next) ||
        redo.objects.size()!=1 || next!=0)
        return 10;
    history.clear();
    if(history.can_undo() || history.can_redo())
        return 11;

    // Duplicate and delete preserve the v2 format when possible.
    Scene legacy;
    legacy.objects.push_back(SceneObject{});
    auto duplicate_index=duplicate(legacy,0);
    if(legacy.version!=2 || duplicate_index!=1)
        return 12;
    delete_subtree(legacy,0);
    if(legacy.objects.size()!=1)
        return 13;
    // Translation snap, local-axis transform, rotation/scale and legacy upgrade.
    Scene transforms;
    transforms.objects.emplace_back();
    GizmoOptions gizmo{};
    apply_gizmo(transforms,0,GizmoMode::translate,GizmoAxis::x,1.6f,gizmo);
    if (transforms.objects[0].position.x!=2.f || transforms.version!=2)
        return 14;
    gizmo.snap=false;
    apply_gizmo(transforms,0,GizmoMode::rotate,GizmoAxis::y,90.f,gizmo);
    if (transforms.version!=3 || transforms.objects[0].node.rotation.y!=90.f)
        return 15;
    gizmo.local=true;
    apply_gizmo(transforms,0,GizmoMode::translate,GizmoAxis::x,2.f,gizmo);
    if (std::abs(transforms.objects[0].position.z+2.f)>0.001f)
        return 16;
    gizmo.snap=true;
    apply_gizmo(transforms,0,GizmoMode::scale,GizmoAxis::z,0.48f,gizmo);
    if (transforms.objects[0].node.scale.z!=1.5f)
        return 17;
    bool rejected=false;
    try {
        apply_gizmo(transforms,0,GizmoMode::translate,GizmoAxis::x,
            std::numeric_limits<float>::quiet_NaN(),gizmo);
    } catch (std::invalid_argument const&) { rejected=true; }
    if (!rejected)
        return 18;
    return 0;
}
