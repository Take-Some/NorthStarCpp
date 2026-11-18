#include "../engine/northstarcpp/modules/viewport/viewport_frame_builder.hpp"
#include <northstarcpp/geometry_scene.hpp>
#include <stdexcept>
#include <string>

#define REQUIRE(x) do { if(!(x)) throw std::runtime_error(#x); } while(false)

int main() {
    using northstarcpp::Scene;
    using northstarcpp::SceneObject;
    using northstarcpp::viewport_detail::ViewportFrameBuilder;

    ViewportFrameBuilder builder;
    Scene scene;
    SceneObject actor;
    actor.node.id=42;
    scene.objects.push_back(actor);
    scene.components.push_back({42,"northstarcpp.editor.material",1,"materials/one.ymat"});
    scene.components.push_back({42,northstarcpp::geometry_primitive_component,1,"first geometry"});
    scene.components.push_back({42,northstarcpp::geometry_primitive_component,1,"second geometry"});
    scene.components.push_back({42,"engine.world.surface",1,"rock_moss"});
    scene.components.push_back({42,"engine.model.visual_mode",1,
                                "first_person_body 0.55 0.98 55"});
    builder.prepare(scene,true,true);
    REQUIRE(builder.is_active(42));
    REQUIRE(builder.has_materials());
    REQUIRE(builder.material(42)==&scene.components[0].payload);
    REQUIRE(builder.procedural(42)==&scene.components[1]);
    REQUIRE(builder.surface(42)!=nullptr && *builder.surface(42)==2u);
    REQUIRE(builder.first_person(42)!=nullptr);
    REQUIRE(builder.first_person(42)->full_body);

    auto before=builder.retained_material_slots();
    REQUIRE(before==1u);
    builder.prepare(scene,true,true);
    REQUIRE(builder.material(42)==&scene.components[0].payload);
    REQUIRE(builder.retained_material_slots()==before);

    // Last material wins; bad/unknown component variants do not poison state.
    scene.components.push_back({42,"northstarcpp.editor.material",1,"materials/two.ymat"});
    builder.prepare(scene,true,false);
    REQUIRE(builder.material(42)==&scene.components.back().payload);
    REQUIRE(builder.first_person(42)==nullptr);
    REQUIRE(builder.procedural(42)==&scene.components[1]);

    // Old ID must not survive frame changes even if the hash node is cached.
    scene.objects.clear();
    scene.components.clear();
    builder.prepare(scene,true,true);
    REQUIRE(!builder.is_active(42));
    REQUIRE(builder.material(42)==nullptr);
    REQUIRE(builder.procedural(42)==nullptr);
    REQUIRE(builder.surface(42)==nullptr);
    REQUIRE(!builder.has_materials());

    builder.reset_project();
    REQUIRE(builder.retained_material_slots()==0u);
    scene.components.push_back({42,"northstarcpp.editor.material",1,
                                std::string(1025,'x')});
    bool rejected=false;
    try {builder.prepare(scene,true,false);}
    catch(std::runtime_error const&) {rejected=true;}
    REQUIRE(rejected);
}
