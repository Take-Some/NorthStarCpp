// NorthStar First Person Platform: the only file needed to change the playable avatar.
// All paths are VFS paths: project assets override matching sharedAssets files.
//
// Edit the resources, controls and camera settings here. Engine runtime code
// never refers to this model or its animation names.
import { Player, Keys } from "@northstar/engine";

// 1. CHARACTER: model, rig, appearance and playable animation clips.
// Namespaced bundle; all resources are visible under Content / characters / abby.
// Switching characters only requires changing this namespace and clip mapping.
const characterRoot = "characters/abby";
const characterAssets = {
    model: `${characterRoot}/models/abby.ydd`,
    skeleton: `${characterRoot}/models/abby.ymt`,
    material: `${characterRoot}/materials/abby.ymat`,
    textures: `${characterRoot}/textures/abby.ytd`,
    animations: [
        {
            name: "idle",
            source: `${characterRoot}/animations/mm-explore.ycd`,
            clip: "abby-mm-explore-idle"
        },
        {
            name: "walk",
            source: `${characterRoot}/animations/mm-explore.ycd`,
            clip: "abby-mm-explore-walk-loop-fw"
        },
        {
            name: "jump",
            source: `${characterRoot}/animations/traversal.ycd`,
            clip: "abby-ground-jump-run"
        },
        {
            name: "fall",
            source: `${characterRoot}/animations/fall-medium.ycd`,
            clip: "abby-fall-medium"
        }
    ]
};

// 2. GAMEPLAY: the root classes read this configuration automatically.
Player.configure({
    enabled: true,
    eyeHeight: 1.85,                         // Eye position above the feet, metres

    character: {
        resources: characterAssets,
        states: {
            idle: "idle",
            walking: "walk",
            jumping: "jump",
            falling: "fall"
        },
        hideInFirstPerson: true
    },

    // 3. CAMERA: switch with V; zoom with the mouse wheel in third person.
    camera: {
        initialMode: "third_person",
        toggleKey: Keys.V,
        transitionSeconds: 0.45,
        minDistance: 0.5,                     // Metres from the character
        maxDistance: 10.0,
        wheelStep: 0.7
    },

    // 4. CONTROLS: mouse look, WASD movement, Space to jump and E to interact.
    look: {
        mouseSensitivity: 0.0024,
        maxPitch: 1.4,                        // Radians
        gamepadYawSpeed: 2.5,
        gamepadPitchSpeed: 2.1
    },
    movement: {
        jumpKey: Keys.Space
    },
    interaction: {
        enabled: true,
        useKey: Keys.E,
        range: 3.6,                          // Metres
        radius: 0.28,                        // Metres
        impulse: 9
    },

    // 5. SESSION: pause, quit and the title displayed in the game window.
    session: {
        pauseKey: Keys.Escape,
        exitKey: Keys.F10,
        title: "NorthStar First Person Platform | WASD Move | V View | Wheel Zoom | Space Jump | E Push"
    }
});
