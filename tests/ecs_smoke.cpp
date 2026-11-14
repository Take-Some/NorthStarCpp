// NorthStarCpp
// Author: Kayla Verner
// Updated: 2026

#include <northstarcpp/ecs.hpp>
#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>

struct Health { float value; int state; };

int main()
{
    using namespace northstarcpp;
    EcsWorld world;
    if (world.provider_name().empty())
        return 1;

    auto base = make_startup_scene();
    world.load(base);
    if (world.size() != base.objects.size() ||
        world.object(world.at(0)).name != base.objects[0].name)
        return 2;

    auto player = world.at(0);
    auto secondary = world.at(1);
    auto third = world.at(2);
    if (!world.alive(player) || !world.alive(secondary))
        return 3;
    auto objects = world.query({NS_COMPONENT_NAME,NS_COMPONENT_POSITION,NS_COMPONENT_BODY});
    if (objects.size() != base.objects.size())
        return 4;

    world.register_component("game.health", sizeof(Health), alignof(Health));
    Health health{80.f, 2};
    world.set_component(third,"game.health",&health,sizeof(health));
    Health result{};
    world.get_component(third,"game.health",&result,sizeof(result));
    if (result.value != 80.f || result.state != 2 ||
        world.query({"game.health"}).size() != 1)
        return 5;
    world.remove_component(third,"game.health");
    if (world.has_component(third,"game.health") ||
        !world.query({"game.health"}).empty())
        return 6;

    SceneObject inserted;
    inserted.name = "ECS API entity";
    inserted.position = {5.f,0.f,5.f};
    inserted.mass = 0.f;
    auto created = world.create(inserted);
    if (!created || !world.alive(created) || world.size() != base.objects.size()+1)
        return 7;
    world.erase(secondary);
    if (world.alive(secondary) || !world.alive(third) ||
        !world.alive(created) || world.object(third).name != base.objects[2].name)
        return 8;

    auto obj = world.object(created);
    obj.name = "Updated ECS Entity";
    obj.position = {6.f,1.f,7.f};
    world.update(created,obj);
    if (world.object(created).name != obj.name ||
        world.object(created).position.y != 1.f)
        return 9;

    auto unchanged = world.snapshot();
    auto invalid = unchanged;
    invalid.objects[1].player = true; // Another player already exists.
    invalid.objects[1].mass = 1.f;
    bool rejected = false;
    try { world.load(invalid); }
    catch (std::runtime_error const&) { rejected = true; }
    if (!rejected || world.snapshot().objects.size() != unchanged.objects.size() ||
        world.at(0) != player)
        return 10;

    float start = world.object(player).position.x;
    world.step(0.05f,1.f,0.f);
    if (!(world.object(player).position.x > start))
        return 11;

    auto sim = world.snapshot();
    sim.objects[0].position.y = 0.4f;
    world.sync_positions(sim);
    if (world.object(player).position.y != .4f)
        return 12;

    world.load(base);
    if (world.size() != base.objects.size() ||
        world.object(world.at(0)).position.x != 0.f)
        return 13;

    // The scene document is an authoring view; its hierarchy and streaming
    // metadata survive the generic ECS API without depending on Flecs classes.
    Scene authored;
    authored.version = 3;
    authored.name = "ECS Scene Metadata";
    authored.layers = {{"Default",true},{"World",true}};
    authored.cells = {{"SectorA",{-10.f,-10.f,-10.f},{10.f,10.f,10.f},false}};
    SceneObject root;
    root.name = "Root"; root.node.id = 10; root.node.layer = "World";
    root.node.cell = "SectorA"; root.node.asset = "props/root.ydd";
    root.node.script = "scripts/behaviors/root.js";
    root.position = {2.f,0.f,0.f};
    SceneObject child;
    child.name = "Child"; child.node.id = 11; child.node.parent = 10;
    child.node.layer = "World"; child.node.cell = "SectorA";
    child.position = {3.f,0.f,0.f};
    authored.objects = {root,child};
    world.load(authored);
    auto snapshot = world.snapshot();
    if (snapshot.version != 3 || snapshot.name != authored.name ||
        snapshot.cells.size() != 1 || snapshot.objects[1].node.parent != 10 ||
        snapshot.objects[0].node.asset != "props/root.ydd" ||
        snapshot.objects[0].node.script != "scripts/behaviors/root.js")
        return 14;

    auto root_handle = world.at(0);
    auto child_handle = world.at(1);
    if (!world.has_component(root_handle,NS_COMPONENT_SCRIPT) ||
        world.has_component(child_handle,NS_COMPONENT_SCRIPT) ||
        world.query({NS_COMPONENT_SCRIPT}).size()!=1)
        return 20;
    NsScriptComponent root_script{};
    world.get_component(root_handle,NS_COMPONENT_SCRIPT,
                        &root_script,sizeof(root_script));
    if (std::string(root_script.path)!="scripts/behaviors/root.js")
        return 21;
    auto editing = world.object(child_handle);
    editing.position.x = 8.f;
    editing.node.script="scripts/behaviors/child.js";
    world.update(child_handle, editing);
    if (!world.has_component(child_handle,NS_COMPONENT_SCRIPT) ||
        world.query({NS_COMPONENT_SCRIPT}).size()!=2)
        return 22;
    editing.node.script.clear();
    world.update(child_handle,editing);
    if (world.has_component(child_handle,NS_COMPONENT_SCRIPT) ||
        world.query({NS_COMPONENT_SCRIPT}).size()!=1)
        return 23;
    if (world.object(child_handle).node.parent != 10 ||
        world.snapshot().objects[1].position.x != 8.f)
        return 15;

    SceneObject extra;
    extra.name = "Scene Editor New";
    extra.node.layer = "World"; extra.node.cell = "SectorA";
    auto extra_handle = world.create(extra);
    if (world.object(extra_handle).node.id != 12 ||
        world.snapshot().objects.size() != 3)
        return 16;

    bool blocked = false;
    try { world.erase(root_handle); }
    catch (std::runtime_error const&) { blocked = true; }
    if (!blocked || !world.alive(root_handle))
        return 17;
    world.erase(extra_handle);
    if (world.snapshot().objects.size() != 2 ||
        world.snapshot().objects[1].node.id != 11)
        return 18;

    auto updated = world.snapshot();
    if (parse_scene_data(serialize_scene_v3(updated)).objects[1].node.id != 11)
        return 19;

    // V3 worlds must not inherit the old 1024-entity editor-only provider cap.
    Scene crowd;
    crowd.version=3;
    crowd.name="Scripted World Partition Crowd";
    for (std::uint64_t i=1;i<=1100;++i) {
        SceneObject actor;
        actor.name="Actor"+std::to_string(i);
        actor.node.id=i;
        if (i==1000) actor.node.script="scripts/behaviors/crowd.js";
        crowd.objects.push_back(std::move(actor));
    }
    world.load(crowd);
    if (world.size()!=1100 || world.query({NS_COMPONENT_SCRIPT}).size()!=1)
        return 24;
    return 0;
}
