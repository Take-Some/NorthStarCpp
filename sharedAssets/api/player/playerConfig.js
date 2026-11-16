// Default project-independent Player configuration. Projects may replace
// scripts/player/playerConfig.js or the compatible scripts/playerConfig.js.
import { Player, Keys, Scene } from "@northstar/engine";

class DefaultPlayerConfig {
    static create() {
        const hasPawn = Scene.findPawn() !== null;
        return {
            enabled: hasPawn,
            eyeHeight: 1.45,
            character: null,
            camera: {
                initialMode: "third_person",
                toggleKey: Keys.V,
                transitionSeconds: 0.45,
                minDistance: 0.5,
                maxDistance: 10,
                wheelStep: 0.7
            },
            look: {
                mouseSensitivity: 0.0024, maxPitch: 1.4,
                gamepadYawSpeed: 2.5, gamepadPitchSpeed: 2.1,
                aimOnRightMouse: true
            },
            movement: {
                jumpKey: Keys.Space,
                sprintKey: Keys.Shift
            },
            footsteps: {
                enabled: true,
                walkStride: 1.25, runStride: 1.65, sprintStride: 1.9,
                runSpeed: 3.2, sprintSpeed: 5.6
            },
            interaction: {
                enabled: hasPawn,
                useKey: Keys.E, range: 3.6,
                radius: 0.28, impulse: 9
            },
            session: {
                pauseKey: Keys.Escape,
                exitKey: Keys.F10,
                title: "NorthStarCpp | WASD Move | V View | Wheel Zoom"
            }
        };
    }

    static register() { return Player.configure(this.create()); }
}

DefaultPlayerConfig.register();
