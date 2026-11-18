// Pure hierarchy-routing regression. No V8 or renderer required.
#include "../engine/northstarcpp/modules/host/script_api_hierarchy.hpp"
#include <iostream>
#include <stdexcept>
#include <string_view>
using northstarcpp::script_api_hierarchy::legacy_namespace;
int main() {
    struct Case { std::string_view nested, method, legacy; };
    constexpr Case examples[]={
        {"engine.audio.playback","playNac","engine.audio"},
        {"engine.audio.voices","setVoicePosition","engine.audio"},
        {"engine.physics.queries","raycast","engine.physics"},
        {"engine.physics.bodies","applyImpulse","engine.physics"},
        {"engine.physics.contacts","pollContacts","engine.physics"},
        {"engine.input.actions","rebindAction","engine.input"},
        {"engine.input.devices","gamepad","engine.input"},
        {"engine.scene.entities","spawn","engine.scene"},
        {"engine.scene.attachments","attachBone","engine.scene"},
        {"engine.scene.presentation","setAnimation","engine.scene"},
        {"engine.ecs.queries","queryMany","engine.ecs"},
        {"engine.ecs.components","getPosition","engine.ecs"},
        {"engine.camera.director","pushShot","engine.camera"},
        {"engine.camera.cinematics","playCutscene","engine.camera"},
        {"engine.camera.controls","shake","engine.camera"},
        {"engine.camera.lens","lensState","engine.camera"},
        {"engine.player.controller","possess","engine.player"},
        {"engine.player.view","setViewTarget","engine.player"},
        {"engine.player.animation","animationState","engine.player"},
        {"engine.player.movement","jump","engine.player"},
        {"engine.ui.documents","readAsset","engine.ui"},
        {"engine.ui.window","viewportSize","engine.ui"},
        {"engine.session.scenes","loadScene","engine.session"},
        {"engine.session.lifecycle","restart","engine.session"},
        {"engine.save.slots","read","engine.save"},
        {"engine.animation.player","seek","engine.animation"},
        {"engine.character.visuals","pose","engine.character"},
        {"engine.ai.navigation","moveTo","engine.ai"},
        {"engine.procedural.noise","sample2","engine.perlin"},
        {"engine.assets.prefabs","read","engine.prefab"},
        {"engine.weapon.effects","emitShot","engine.weaponfx"},
        {"engine.render.provider","detailLevel","engine.rendering"}
    };
    for(auto const& test:examples)
        if(legacy_namespace(test.nested,test.method)!=test.legacy)
            throw std::runtime_error("Missing nested namespace alias");
    // Fail closed: never reroute a method into a different legacy service.
    constexpr Case negative[]={
        {"engine.physics.queries","applyImpulse",""},
        {"engine.audio.voices","raycast",""},
        {"engine.scene.attachments","spawn",""},
        {"engine.render.shadows","state",""},
        {"engine.audio.voices","stopVoiceOther",""},
        {"engine.audio.voices.extra","stopVoice",""},
        {"engine.audio.voices","stop",""}
    };
    for(auto const& test:negative)
        if(legacy_namespace(test.nested,test.method)!=test.legacy)
            throw std::runtime_error("Unexpected cross-domain fallback");
    struct MigrationCase {std::string_view legacy,method,capability,operation;};
    constexpr MigrationCase migrations[]{
        {"engine.sky","state","engine.render.sky","state"},
        {"engine.sky","configure","engine.render.sky","configure"},
        {"engine.rendering","detailLevel","engine.render.provider","detailLevel"},
        {"engine.render","shaderCatalog","engine.render.shaders","catalog"},
        {"engine.render","shaderPoll","engine.render.shaders","poll"}
    };
    for(auto const& test:migrations) {
        auto const actual=northstarcpp::script_api_hierarchy::canonical_method(
            test.legacy,test.method);
        if(actual.first!=test.capability || actual.second!=test.operation)
            throw std::runtime_error("Missing legacy-to-canonical mapping");
    }
    if(!northstarcpp::script_api_hierarchy::canonical_method(
        "engine.audio","play").first.empty())
        throw std::runtime_error("Unexpected reverse mapping");
    std::cout<<"PASS engine API hierarchy: "<<
        (sizeof(examples)/sizeof(Case))<<" routes, fail-closed boundaries\n";
}
