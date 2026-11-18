#pragma once
// Provider-neutral compatibility routing for the multi-level engine.* API.
//
// A nested capability is looked up first (including any external plugin
// implementation). Only if no handler owns that exact namespace/method is
// the legacy one-level method used. No scripting bridge or renderer backend
// owns a second implementation of a capability.
#include <array>
#include <string_view>
#include <utility>

namespace northstarcpp::script_api_hierarchy {

struct Group {
    std::string_view legacy;
    std::string_view nested;
    std::string_view methods; // delimiter-separated whole method names
};

inline constexpr std::array<Group, 33> groups{{
    {"engine.audio", "engine.audio.playback", "play|playNac"},
    {"engine.audio", "engine.audio.voices",
        "supportsVoices|stopVoice|setVoiceGain|setVoicePosition"},
    {"engine.physics", "engine.physics.queries",
        "raycast|sweepSphere|sweepSphereChannel"},
    {"engine.physics", "engine.physics.bodies",
        "applyImpulse|setBodyMaterial"},
    {"engine.physics", "engine.physics.contacts", "pollContacts"},
    {"engine.input", "engine.input.actions",
        "actionDown|rebindAction|setContextEnabled"},
    {"engine.input", "engine.input.devices",
        "isDown|mouseDown|pointerDelta|wheelDelta|gamepad"},
    {"engine.scene", "engine.scene.entities",
        "spawn|destroy|objectCount|objects|moveRuntime|rotateRuntime|"
        "moveStatic|rotateStatic|setLayerVisible|setColor"},
    {"engine.scene", "engine.scene.presentation",
        "setAnimation|setVisualMode"},
    {"engine.scene", "engine.scene.attachments",
        "attachBone|attachmentPoint|detachBone"},
    {"engine.ecs", "engine.ecs.queries", "entities|query|queryMany"},
    {"engine.ecs", "engine.ecs.components",
        "has|get|getScript|getPosition|setPosition"},
    {"engine.camera", "engine.camera.director",
        "pushShot|clearShot|clearShots|activeShot|applyPreset"},
    {"engine.camera", "engine.camera.cinematics",
        "playCutscene|stopCutscene|pauseCutscene|seekCutscene|"
        "cutsceneState|playTrack"},
    {"engine.camera", "engine.camera.controls",
        "setMode|mode|setFollowDistance|followDistance|setAngles|angles|"
        "cut|setZoom|shake|recoil"},
    {"engine.camera", "engine.camera.lens", "lensState"},
    {"engine.camera", "engine.camera.status", "available"},
    {"engine.player", "engine.player.controller",
        "controlledEntity|controlledIndex|possess|unpossess"},
    {"engine.player", "engine.player.view",
        "viewTarget|setViewTarget|resetViewTarget"},
    {"engine.player", "engine.player.animation", "animationState"},
    {"engine.player", "engine.player.movement",
        "sprint|move|motion|motionState|ground|jump|eye|setEyeHeight"},
    {"engine.ui", "engine.ui.documents",
        "readAsset|apply|hotReloadEnabled"},
    {"engine.ui", "engine.ui.window", "setTitle|viewportSize"},
    {"engine.session", "engine.session.scenes", "loadScene|scene"},
    {"engine.session", "engine.session.lifecycle", "pause|restart|quit"},
    {"engine.save", "engine.save.slots", "write|read|exists|remove"},
    {"engine.animation", "engine.animation.player", "play|pause|seek|stop|state"},
    {"engine.character", "engine.character.visuals", "load|hide|pose|clear"},
    {"engine.ai", "engine.ai.navigation", "moveTo|stop|isMoving"},
    {"engine.perlin", "engine.procedural.noise", "sample2"},
    {"engine.prefab", "engine.assets.prefabs", "read"},
    {"engine.weaponfx", "engine.weapon.effects", "emitShot"},
    {"engine.rendering", "engine.render.provider",
        "engineVersion|detailLevel|setDetailLevel"},
}};

inline bool contains(std::string_view delimited, std::string_view method) noexcept {
    while (!delimited.empty()) {
        const auto end=delimited.find('|');
        const auto item=delimited.substr(0,end);
        if(item==method)return true;
        if(end==std::string_view::npos)return false;
        delimited.remove_prefix(end+1);
    }
    return false;
}

// Migration aliases for canonical render services that previously lived
// under a different capability or method name. The receiver may be a native
// implementation or an external plugin, never a second copy of the service.
struct Migration {
    std::string_view old_capability, old_method;
    std::string_view canonical_capability, canonical_method;
};
inline constexpr std::array<Migration,12> migrations{{
    {"engine.sky","configure","engine.render.sky","configure"},
    {"engine.sky","setTimeOfDay","engine.render.sky","setTimeOfDay"},
    {"engine.sky","advance","engine.render.sky","advance"},
    {"engine.sky","reset","engine.render.sky","reset"},
    {"engine.sky","state","engine.render.sky","state"},
    {"engine.rendering","engineVersion","engine.render.provider","engineVersion"},
    {"engine.rendering","setDetailLevel","engine.render.provider","setDetailLevel"},
    {"engine.rendering","detailLevel","engine.render.provider","detailLevel"},
    {"engine.render","shaderCatalog","engine.render.shaders","catalog"},
    {"engine.render","shaderSource","engine.render.shaders","source"},
    {"engine.render","shaderQueueCompiled","engine.render.shaders","queueCompiled"},
    {"engine.render","shaderPoll","engine.render.shaders","poll"},
}};
inline std::pair<std::string_view,std::string_view>
canonical_method(std::string_view capability,std::string_view method) noexcept {
    for(auto const& item:migrations)
        if(item.old_capability==capability && item.old_method==method)
            return {item.canonical_capability,item.canonical_method};
    return {};
}

// Return an explicit legacy namespace for a nested call, never the reverse.
// Exact native and plugin registrations always take priority over this alias.
inline std::string_view legacy_namespace(std::string_view capability,
                                         std::string_view method) noexcept {
    for(auto const& group:groups)
        if(group.nested==capability && contains(group.methods,method))
            return group.legacy;
    return {};
}
} // namespace northstarcpp::script_api_hierarchy
