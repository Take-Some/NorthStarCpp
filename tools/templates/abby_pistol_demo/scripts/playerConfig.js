// NorthStar First Person Platform: the only file needed to change the playable avatar.
// All paths are VFS paths: project assets override matching sharedAssets files.
//
// Edit the resources, controls and camera settings here. Engine runtime code
// never refers to this model or its animation names.
import { Player, Keys } from "@northstar/engine";

// 1. OPTIONAL CHARACTER VISUAL: model, rig, appearance and animation.
 // This configuration associates a reusable Abby prefab with this demo Pawn.
 // Abby itself does not become a player or require a weapon.
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
    // Camera Rig component owns eye height and field of view.

    character: {
        // One visual prefab for both camera modes; no second Abby entity.
        // This remains independent of Pawn, Camera Rig and equipped weapons.
        prefab: "prefabs/abby.nprefab",
        // Camera-space presentation of that same mesh's skinned arm triangles.
        firstPersonViewmodel: {forward:0.55,drop:0.98,armPitch:55},
        resources: characterAssets,
        states: {
            idle: "idle",
            walking: "walk",
            jumping: "jump",
            falling: "fall"
        },
        renderedByProject: true // Native YDD/YMT scene renderer on D3D12
    },

    // 3. CAMERA: switch with V; zoom with the mouse wheel in third person.
    camera: {
        initialMode: "first_person",
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
        jumpKey: Keys.Space,
        sprintKey: Keys.Shift
    },
    // Standard ground-aware footsteps from sharedAssets/audio/footsteps/*.nac.
    footsteps: {
        enabled: true,
        walkStride: 1.25,
        runStride: 1.65,
        sprintStride: 1.9,
        runSpeed: 5.0,
        sprintSpeed: 8.0
    },
    interaction: {
        enabled: true,
        useKey: Keys.E,
        range: 3.6,                          // Metres
        radius: 0.28,                        // Metres
        impulse: 9
    },

    // 5. WEAPON: game-owned rules; the engine only supplies generic scene,
    // physics, input, UI and audio services. No engine.weapon API.
    weapon: {
        enabled: true,
        displayName: "Pistol",
        prefab: "prefabs/pistol.nprefab",
        // Generic socket, resolved against the active Abby skeletal pose.
        // The local weapon grip pivot can be tuned in the project without
        // recompiling NorthStarCPP; it never follows a free camera offset.
        grip: {
            bone: "r_hand_prop_attachment",
            offset: {x:0,y:0,z:0},
            // Inverse of Abby's idle r_hand_prop_attachment orientation,
            // accounting for the first-person 55-degree arm presentation.
            // Gun local +Z (barrel) now points along camera forward (+Z).
            rotation: {x:44.193,y:5.215,z:161.923},
            // Small sight-line convergence toward the screen centre;
            // the engine's camera-forward socket mode keeps +Z aimed ahead.
            firstPersonRotation: {x:0,y:12,z:0}
        },
        audio: "audio/weapon/pistol/pistol.nac",
        magazineSize: 12,
        reserveAmmo: 72,
        roundsPerSecond: 5,
        reloadSeconds: 1.80,
        animations: {
            dictionary: "animations/weapon/pistol/military-pistol.ycd",
            idle: "idle",
            fire: "p-fire",
            reload: "p-reload",
            empty: "p-semiauto-empty"
        },
        range: 110,
        sweepRadius: 0.055,
        impactImpulse: 14,
        fireGain: 0.95
    },

    // 6. SESSION: pause, quit and the title displayed in the game window.
    session: {
        pauseKey: Keys.Escape,
        exitKey: Keys.F10,
        title: "Abby Pistol Demo | WASD move | Mouse aim | LMB fire | RMB aim | R reload | V camera"
    }
});
