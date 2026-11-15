// Project-configurable player defaults. No particular avatar is assumed.
import { Player, Keys } from "@northstar/engine";

class DefaultPlayerConfig {
    static register() {
        Player.configure({
            enabled: false,
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
            movement: {jumpKey: Keys.Space},
            interaction: {
                enabled: false,
                useKey: Keys.E,
                range: 3.6,
                radius: 0.28,
                impulse: 9
            },
            session: {
                pauseKey: Keys.Escape,
                exitKey: Keys.F10,
                title: "NorthStarCpp | WASD Move | V View | Wheel Zoom"
            }
        });
    }
}

DefaultPlayerConfig.register();
