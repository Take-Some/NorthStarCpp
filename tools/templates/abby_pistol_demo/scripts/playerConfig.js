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
            name: "run",
            source: `${characterRoot}/animations/mm-explore.ycd`,
            clip: "abby-mm-explore-run-loop-fw"
        },
        {
            name: "sprint",
            source: `${characterRoot}/animations/mm-explore.ycd`,
            clip: "abby-mm-explore-sprint-loop-fw"
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
        prefab: "prefabs/characters/abby.nprefab",
        // True first-person presentation: original full body remains at the Pawn.
        // The independent camera is at Abby's eyes; no mesh displacement.
        // The head alone is hidden locally to prevent self-occlusion.
        // Weapon held ready: in front of the torso, below the eye.
        // All distances are metres in the camera frame. These authored
        // values are consumed by the generic two-bone grip solver.
        firstPersonViewmodel: {forward:0.52,drop:0.24,right:0.12,armPitch:0},
        resources: characterAssets,
        states: {
            idle: "idle",
            // The renderer adds a generic two-hand attachment overlay; the
            // authored exploration clip remains a lower-body base layer.
            armed_third_person_pistol: "idle",
            armed_first_person_pistol: "idle",
            walking: "walk",
            running: "run",
            sprinting: "sprint",
            jumping: "jump",
            falling: "fall"
        },
        // Speeds reconstructed from stance-foot motion in Abby's in-place YCD.
        // This is measured animation motion, not a verified runtime game constant.
        locomotion: {
            walkSpeed: 1.48, runSpeed: 2.95, sprintSpeed: 4.44,
            walkToRun: 2.215, runToSprint: 3.695,
            movementThreshold: 0.20, hysteresis: 0.18,
            smoothingSeconds: 0.12,
            minPlaybackRate: 0.65, maxPlaybackRate: 1.5
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
        // RMB ADS: half-speed fine adjustment of the iron sights.
        adsSensitivityMultiplier: 0.52,
        maxPitch: 1.4,                        // Radians
        gamepadYawSpeed: 2.5,
        gamepadPitchSpeed: 2.1
    },
    movement: {
        jumpKey: Keys.Space,
        walkKey: Keys.Control,
        walkScale: 1.48 / 2.95,
        sprintKey: Keys.Shift
    },
    // Standard ground-aware footsteps from sharedAssets/audio/footsteps/*.nac.
    footsteps: {
        enabled: true,
        walkStride: 1.25,
        runStride: 1.65,
        sprintStride: 1.9,
        runSpeed: 2.215,
        sprintSpeed: 3.695
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
        prefab: "prefabs/weapon/pistol.nprefab",
        // Generic socket, resolved against the active Abby skeletal pose.
        // The local weapon grip pivot can be tuned in the project without
        // recompiling NorthStarCPP; it never follows a free camera offset.
        grip: {
            bone: "r_hand_prop_attachment",
            offset: {x:0,y:0,z:0},
            // TPS socket-space calibration after the YDD/YMT basis fix.
            // Gun-local +Z points along the character's forward direction.
            // FPS uses its separate camera-aligned rotation below.
            rotation: {x:-107.270,y:50.242,z:124.884},
            // Small sight-line convergence toward the screen centre;
            // the engine's camera-forward socket mode keeps +Z aimed ahead.
            firstPersonRotation: {x:0,y:0,z:0},
            // For ADS, rear sight at pistol-local y≈0.068 m must be
            // at eye height and the front sight must be visible behind it.
            // The socket sits about 0.056 m above the right wrist; together
            // those authored dimensions place the wrist ~0.124 m below eye.
            aimDownSights: {forward:0.52,drop:0.115,right:0.063,armPitch:0}
        },
        audio: "audio/weapon/pistol/pistol.nac",
        hudAsset: "ui/weapon_hud.nui",
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
        // Weapon-local barrel exit, relative to the animated pistol pivot.
        // This is authored geometry metadata, never a camera offset.
        // Calibrate on the real YDD muzzle opening in GPU WeaponLab.
        muzzleLocal: {x:0,y:0.0738,z:0.238},
        // Gun-local port position, resolved through the active weapon socket;
        // not a camera-based approximation. Calibrate in WeaponLab.
        ejectionLocal: {x:0.047,y:0.064,z:0.035},
        casingType: "pistol",
        shellImpulse: 1.0,
        // Common weapon angular recoil, plus a short camera impulse.
        recoil: {
            // Physical 3-phase action: sharp backstroke, lateral accent,
            // damped spring return. Ballistics resolves BEFORE any of it.
            pitchRadians:0.0072, yawRadians:0,
            recoverySeconds:0.17, amplitude:0.004, duration:0.07,
            weaponBackMeters:0.030, armImpulse:0.68,
            snapSeconds:0.043, accentSeconds:0.036,
            settleSeconds:0.16, aftershock:0.23,
            patternYawRadians:0.0022,
            // Controlled sequence across quick follow-up shots; never RNG.
            shotPattern: [
                {pitchScale:1.0,yawScale:0},
                {pitchScale:1.13,yawScale:0.34},
                {pitchScale:1.22,yawScale:-0.20},
                {pitchScale:1.15,yawScale:0.28},
                {pitchScale:1.28,yawScale:-0.15}
            ],
            aimDownSights: {
                pitchRadians:0.0058, yawRadians:0,
                recoverySeconds:0.17, amplitude:0.002, duration:0.065,
                weaponBackMeters:0.025, armImpulse:0.56
            }
        },
        aimSweepRadius: 0.006,
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
