// IDE completion and diagnostics for NorthStarCpp's class-based JavaScript SDK.
// Runtime import resolution is handled by NorthStar ScriptRuntime, not Node.js.
// Current runtime supports static named imports from "@northstar/engine" only.
// Input enum declarations are GENERATED from the classes in input/*.js.
// Run: python tools/dev/generate_input_typings.py
// Verify: python tools/dev/generate_input_typings.py --check
declare module "@northstar/engine" {
    export interface Vector3 {
        x: number;
        y: number;
        z: number;
    }

    export interface CameraAngles {
        yaw: number;
        pitch: number;
    }

    export type CameraView = "first_person" | "third_person";

    export interface CameraOptions {
        initialMode?: CameraView;
        toggleKey?: number;
        transitionSeconds?: number;
        minDistance?: number;
        maxDistance?: number;
        wheelStep?: number;
    }

    export interface CameraViewController {
        start(): void;
        stop(): void;
        update(): void;
        mode(): string;
        distance(): number;
        setMode(mode: CameraView, duration?: number): CameraView;
        toggle(): CameraView;
    }

    export interface CameraCutsceneOptions {
        loop?: boolean;
        entrySeconds?: number;
        exitSeconds?: number;
    }
    export interface CameraCutsceneState {
        active: boolean;
        time: number;
        shot: string;
    }
    export interface CameraLensState {
        active: boolean;
        focusMetres: number;
        apertureF: number;
        exposureEV: number;
        motionBlur: number;
        letterbox: number;
        dofStrength: number;
    }
    /** Authored event delivered with camera.cues on the game thread. */
    export interface CameraCutsceneCue {
        time: number;
        type: "animation" | "audio" | "dialogue" | "event" | "overlay";
        lane: string;
        target: string;
        action: string;
        value: string;
    }

    export class Camera {
        static available(): boolean;
        static mode(): string;
        static setMode(mode: string, seconds?: number): void;
        static angles(): CameraAngles;
        static setAngles(yaw: number, pitch: number): void;
        static followDistance(): number;
        static setFollowDistance(distance: number): void;
        static setZoom(amount: number): void;
        /** One-shot post-effect offset, metres and seconds. */
        static shake(amplitudeMetres: number, seconds: number): void;
        /** Physical first-person sight kick, recovered by a spring. */
        static recoil(pitchRadians: number, yawRadians?: number,
            recoverySeconds?: number): void;
        /** Teleport/shot cut: clear follow interpolation but preserve active track time. */
        static cut(): void;
        /** Play one cinematic rail from the active VFS. */
        static playTrack(path: string, loop?: boolean,
                         blendSeconds?: number): void;
        /** Play a multi-shot cutscene; returns to the gameplay view. */
        static playCutscene(path: string, options?: CameraCutsceneOptions): void;
        static stopCutscene(exitSeconds?: number): void;
        static pauseCutscene(paused?: boolean): void;
        static seekCutscene(seconds: number): void;
        static cutsceneState(): CameraCutsceneState;
        static lensState(): CameraLensState;
        /** Authoring events delivered in order. Dispose the listener on stop. */
        static onCue(handler: (cue: CameraCutsceneCue) => void): () => boolean;
        /** World-space pose uses radians for yaw, pitch and roll. */
        static pushShot(
            source: string,
            pose: {position: Vector3; yaw: number; pitch: number;
                   roll?: number; fovY?: number},
            priority?: number, blendSeconds?: number): void;
        static clearShot(source: string): void;
        static clearShots(): void;
        static activeShot(): string;
        /** VFS gameplay/camera_presets.json. */
        static applyPreset(id: string): void;
        static createViewController(options?: CameraOptions): CameraViewController;
        static lookAt(source: string, position: Vector3, target: Vector3,
            options?: {fovY?: number;priority?: number;blendSeconds?: number}): void;
    }

    // BEGIN GENERATED INPUT ENUMS: tools/dev/generate_input_typings.py
    /** Keyboard Virtual-Key codes, generated from input/Keys.js. */
    export class Keys {
        static readonly Digit0: 0x30;
        static readonly Digit1: 0x31;
        static readonly Digit2: 0x32;
        static readonly Digit3: 0x33;
        static readonly Digit4: 0x34;
        static readonly Digit5: 0x35;
        static readonly Digit6: 0x36;
        static readonly Digit7: 0x37;
        static readonly Digit8: 0x38;
        static readonly Digit9: 0x39;
        static readonly A: 0x41;
        static readonly B: 0x42;
        static readonly C: 0x43;
        static readonly D: 0x44;
        static readonly E: 0x45;
        static readonly F: 0x46;
        static readonly G: 0x47;
        static readonly H: 0x48;
        static readonly I: 0x49;
        static readonly J: 0x4A;
        static readonly K: 0x4B;
        static readonly L: 0x4C;
        static readonly M: 0x4D;
        static readonly N: 0x4E;
        static readonly O: 0x4F;
        static readonly P: 0x50;
        static readonly Q: 0x51;
        static readonly R: 0x52;
        static readonly S: 0x53;
        static readonly T: 0x54;
        static readonly U: 0x55;
        static readonly V: 0x56;
        static readonly W: 0x57;
        static readonly X: 0x58;
        static readonly Y: 0x59;
        static readonly Z: 0x5A;
        static readonly F1: 0x70;
        static readonly F2: 0x71;
        static readonly F3: 0x72;
        static readonly F4: 0x73;
        static readonly F5: 0x74;
        static readonly F6: 0x75;
        static readonly F7: 0x76;
        static readonly F8: 0x77;
        static readonly F9: 0x78;
        static readonly F10: 0x79;
        static readonly F11: 0x7A;
        static readonly F12: 0x7B;
        static readonly F13: 0x7C;
        static readonly F14: 0x7D;
        static readonly F15: 0x7E;
        static readonly F16: 0x7F;
        static readonly F17: 0x80;
        static readonly F18: 0x81;
        static readonly F19: 0x82;
        static readonly F20: 0x83;
        static readonly F21: 0x84;
        static readonly F22: 0x85;
        static readonly F23: 0x86;
        static readonly F24: 0x87;
        static readonly Cancel: 0x03;
        static readonly Backspace: 0x08;
        static readonly Tab: 0x09;
        static readonly Clear: 0x0C;
        static readonly Enter: 0x0D;
        static readonly Shift: 0x10;
        static readonly Control: 0x11;
        static readonly Alt: 0x12;
        static readonly Pause: 0x13;
        static readonly CapsLock: 0x14;
        static readonly Kana: 0x15;
        static readonly Hangeul: 0x15;
        static readonly Hangul: 0x15;
        static readonly ImeOn: 0x16;
        static readonly Junja: 0x17;
        static readonly Final: 0x18;
        static readonly Hanja: 0x19;
        static readonly Kanji: 0x19;
        static readonly ImeOff: 0x1A;
        static readonly Escape: 0x1B;
        static readonly Convert: 0x1C;
        static readonly Nonconvert: 0x1D;
        static readonly Accept: 0x1E;
        static readonly Modechange: 0x1F;
        static readonly Space: 0x20;
        static readonly PageUp: 0x21;
        static readonly PageDown: 0x22;
        static readonly End: 0x23;
        static readonly Home: 0x24;
        static readonly ArrowLeft: 0x25;
        static readonly ArrowUp: 0x26;
        static readonly ArrowRight: 0x27;
        static readonly ArrowDown: 0x28;
        static readonly Select: 0x29;
        static readonly Print: 0x2A;
        static readonly Execute: 0x2B;
        static readonly PrintScreen: 0x2C;
        static readonly Insert: 0x2D;
        static readonly Delete: 0x2E;
        static readonly Help: 0x2F;
        static readonly LeftMeta: 0x5B;
        static readonly RightMeta: 0x5C;
        static readonly ContextMenu: 0x5D;
        static readonly Sleep: 0x5F;
        static readonly Numpad0: 0x60;
        static readonly Numpad1: 0x61;
        static readonly Numpad2: 0x62;
        static readonly Numpad3: 0x63;
        static readonly Numpad4: 0x64;
        static readonly Numpad5: 0x65;
        static readonly Numpad6: 0x66;
        static readonly Numpad7: 0x67;
        static readonly Numpad8: 0x68;
        static readonly Numpad9: 0x69;
        static readonly NumpadMultiply: 0x6A;
        static readonly NumpadAdd: 0x6B;
        static readonly NumpadSeparator: 0x6C;
        static readonly NumpadSubtract: 0x6D;
        static readonly NumpadDecimal: 0x6E;
        static readonly NumpadDivide: 0x6F;
        static readonly NavigationView: 0x88;
        static readonly NavigationMenu: 0x89;
        static readonly NavigationUp: 0x8A;
        static readonly NavigationDown: 0x8B;
        static readonly NavigationLeft: 0x8C;
        static readonly NavigationRight: 0x8D;
        static readonly NavigationAccept: 0x8E;
        static readonly NavigationCancel: 0x8F;
        static readonly NumLock: 0x90;
        static readonly ScrollLock: 0x91;
        static readonly OemNecEqual: 0x92;
        static readonly OemFjJisho: 0x92;
        static readonly OemFjMasshou: 0x93;
        static readonly OemFjTouroku: 0x94;
        static readonly OemFjLoya: 0x95;
        static readonly OemFjRoya: 0x96;
        static readonly LeftShift: 0xA0;
        static readonly RightShift: 0xA1;
        static readonly LeftControl: 0xA2;
        static readonly RightControl: 0xA3;
        static readonly LeftAlt: 0xA4;
        static readonly RightAlt: 0xA5;
        static readonly BrowserBack: 0xA6;
        static readonly BrowserForward: 0xA7;
        static readonly BrowserRefresh: 0xA8;
        static readonly BrowserStop: 0xA9;
        static readonly BrowserSearch: 0xAA;
        static readonly BrowserFavorites: 0xAB;
        static readonly BrowserHome: 0xAC;
        static readonly VolumeMute: 0xAD;
        static readonly VolumeDown: 0xAE;
        static readonly VolumeUp: 0xAF;
        static readonly MediaNextTrack: 0xB0;
        static readonly MediaPreviousTrack: 0xB1;
        static readonly MediaStop: 0xB2;
        static readonly MediaPlayPause: 0xB3;
        static readonly LaunchMail: 0xB4;
        static readonly LaunchMediaSelect: 0xB5;
        static readonly LaunchApp1: 0xB6;
        static readonly LaunchApp2: 0xB7;
        static readonly Semicolon: 0xBA;
        static readonly Equal: 0xBB;
        static readonly Comma: 0xBC;
        static readonly Minus: 0xBD;
        static readonly Period: 0xBE;
        static readonly Slash: 0xBF;
        static readonly Backquote: 0xC0;
        static readonly BracketLeft: 0xDB;
        static readonly Backslash: 0xDC;
        static readonly BracketRight: 0xDD;
        static readonly Quote: 0xDE;
        static readonly Oem8: 0xDF;
        static readonly OemAx: 0xE1;
        static readonly IntlBackslash: 0xE2;
        static readonly IcoHelp: 0xE3;
        static readonly Ico00: 0xE4;
        static readonly Processkey: 0xE5;
        static readonly IcoClear: 0xE6;
        static readonly Packet: 0xE7;
        static readonly OemReset: 0xE9;
        static readonly OemJump: 0xEA;
        static readonly OemPa1: 0xEB;
        static readonly OemPa2: 0xEC;
        static readonly OemPa3: 0xED;
        static readonly OemWsctrl: 0xEE;
        static readonly OemCusel: 0xEF;
        static readonly OemAttn: 0xF0;
        static readonly OemFinish: 0xF1;
        static readonly OemCopy: 0xF2;
        static readonly OemAuto: 0xF3;
        static readonly OemEnlw: 0xF4;
        static readonly OemBacktab: 0xF5;
        static readonly Attn: 0xF6;
        static readonly Crsel: 0xF7;
        static readonly Exsel: 0xF8;
        static readonly Ereof: 0xF9;
        static readonly Play: 0xFA;
        static readonly Zoom: 0xFB;
        static readonly Noname: 0xFC;
        static readonly Pa1: 0xFD;
        static readonly OemClear: 0xFE;
        static readonly Back: 0x08;
        static readonly Return: 0x0D;
        static readonly Ctrl: 0x11;
        static readonly Menu: 0x12;
        static readonly Left: 0x25;
        static readonly Right: 0x27;
        static readonly Up: 0x26;
        static readonly Down: 0x28;
        static readonly Prior: 0x21;
        static readonly Next: 0x22;
        static readonly Snapshot: 0x2C;
        static readonly ShiftLeft: 0xA0;
        static readonly ShiftRight: 0xA1;
        static readonly ControlLeft: 0xA2;
        static readonly ControlRight: 0xA3;
        static readonly AltLeft: 0xA4;
        static readonly AltRight: 0xA5;
        static readonly MetaLeft: 0x5B;
        static readonly MetaRight: 0x5C;
        static readonly DeleteForward: 0x2E;
        static readonly Oem1: 0xBA;
        static readonly OemPlus: 0xBB;
        static readonly OemComma: 0xBC;
        static readonly OemMinus: 0xBD;
        static readonly OemPeriod: 0xBE;
        static readonly Oem2: 0xBF;
        static readonly Oem3: 0xC0;
        static readonly Oem4: 0xDB;
        static readonly Oem5: 0xDC;
        static readonly Oem6: 0xDD;
        static readonly Oem7: 0xDE;
        static readonly Oem102: 0xE2;
    }

    /** Gamepad bitmasks, generated from input/GamepadButtons.js. */
    export class GamepadButtons {
        static readonly DPadUp: 0x0001;
        static readonly DPadDown: 0x0002;
        static readonly DPadLeft: 0x0004;
        static readonly DPadRight: 0x0008;
        static readonly Start: 0x0010;
        static readonly Back: 0x0020;
        static readonly LeftThumb: 0x0040;
        static readonly RightThumb: 0x0080;
        static readonly LeftShoulder: 0x0100;
        static readonly RightShoulder: 0x0200;
        static readonly A: 0x1000;
        static readonly B: 0x2000;
        static readonly X: 0x4000;
        static readonly Y: 0x8000;
        static readonly Jump: 0x1000;
        static readonly Interact: 0x4000;
    }
    // END GENERATED INPUT ENUMS

    export interface GamepadState {
        buttons: number;
        leftX: number;
        leftY: number;
        rightX: number;
        rightY: number;
    }

    export class InputAction {
        constructor(key: number, gamepadButton?: number | null);
        held(gamepad?: GamepadState | null): boolean;
        pressed(gamepad?: GamepadState | null): boolean;
        reset(): void;
    }
    export class Input {
        static readonly Keys: typeof Keys;
        static readonly GamepadButtons: typeof GamepadButtons;
        static isDown(key: number): boolean;
        static mouseDown(button: number): boolean;
        static pointerDelta(): {x: number; y: number};
        static wheelDelta(): number;
        static gamepad(index: number): GamepadState | null;
        static firstGamepad(maxPlayers?: number): GamepadState | null;
        static hasButton(gamepad: GamepadState | null, button: number): boolean;
        static action(key: number, gamepadButton?: number | null,
                      gamepad?: GamepadState | null): boolean;
        static axis(positiveKey: number, negativeKey: number, analogue?: number,
                    gamepadPositive?: number | null, gamepadNegative?: number | null,
                    gamepad?: GamepadState | null): number;
        static createAction(key: number, gamepadButton?: number | null): InputAction;
        /** Normalize diagonal movement without amplifying analog input. */
        static normalized2D(x: number, y: number): {x: number; y: number};
    }

    export interface AudioRequest {
        asset: string;
        /** NAC track ID or prefix. Ignored options are rejected for WAV. */
        clip?: string;
        variation?: number;
        gain?: number;
        /** Optional 3D source position for NAC voice playback. */
        position?: Vector3;
    }
    export class AudioVoice {
        private constructor();
        readonly id:number;
        readonly active:boolean;
        stop():boolean;
        setGain(gain:number):this;
        setPosition(position:Vector3):this;
        dispose():boolean;
    }
    export class Audio {
        static supportsVoices():boolean;
        static playVoice(request:AudioRequest | string,
            options?:Omit<AudioRequest,"asset">):AudioVoice;
        /** Policy-free playback, requires a native provider. */
        static play(request: AudioRequest | string,
            options?: Omit<AudioRequest, "asset">): number | void;
        static playWav(path: string): number;
        /** Spatial NAC playback where provider voices ABI 2 is available. */
        static playNacAt(path:string,clip:string,position:Vector3,
            variation?:number,gain?:number):number;
        /** Play a matching clip variant from a NEF8/NAC pack through engine.audio. */
        static playNac(path: string, prefix: string,
                       variation?: number, gain?: number): number;
    }

    export interface PlayerFootstepEvent {
        actor: number;
        sequence: number;
        surface: "dirt" | "grass" | "metal" | "mud" | "snow" |
                 "stone" | "water" | "wood";
        /** Physical surface material from the hit triangle, 0 if unknown. */
        materialId?: number;
        triangleIndex?: number;
        gait: "walk" | "run" | "sprint";
        speed: number;
        position: Vector3;
    }

    export class Events {
        static on(event: string, callback: (payload: any) => void): () => void;
        static off(event: string, callback: (payload: any) => void): boolean;
        static once(event: string, callback: (payload: any) => void): () => void;
        static emit(event: string, payload?: unknown): number;
        static define(event: string, validator?: ((payload: any) => boolean) | null): void;
        static scope(): {
            on(event: string, callback: (payload: any) => void): () => boolean;
            dispose(): void;
        };
        static history(limit?: number):
            ReadonlyArray<Readonly<{sequence: number; name: string}>>;
        static stats(): Readonly<{
            declared: number;
            emitted: number;
            emittedByEvent: Readonly<Record<string, number>>;
            subscribers: number;
            byEvent: Readonly<Record<string, number>>;
        }>;
    }

    export interface AnimationClip {
        name: string;
        source: string; // Virtual path to .ycd
        clip: string;   // Clip identifier inside the .ycd
    }

    export interface CharacterResources {
        model: string;     // Virtual path to .ydd
        skeleton: string;  // Virtual path to .ymt
        material: string;  // Virtual path to .ymat
        textures: string;  // Virtual path to .ytd
        animations: AnimationClip[];
    }

    export interface CharacterStates {
        /** Project-defined animation labels remain authored data. */
        [state:string]:string | undefined;
        idle?: string;
        walking?: string;
        running?: string;
        sprinting?: string;
        jumping?: string;
        falling?: string;
    }

    export interface CharacterTuning {
        walkSpeed?: number;
        walkToRun?: number;
        runToSprint?: number;
        gaitHysteresis?: number;
        teleportSpeed?: number;
        airborneSpeed?: number;
        headingSeconds?: number;
        jumpHoldSeconds?: number;
    }

    export interface CharacterAnimationContext {
        dt: number;
        entityIndex: number;
        position: Vector3;
        heading: number;
        speed: number;
        moving: boolean;
        airborne: boolean;
        animation: string;
        elapsed: number;
        suggested: string;
    }

    /** Character-owned locomotion and optional, authored contact sounds. */
    export interface CharacterFootstepOptions {
        enabled?: boolean;
        walkStride?: number;
        runStride?: number;
        sprintStride?: number;
        runSpeed?: number;
        sprintSpeed?: number;
        sounds?: Readonly<Record<string, string | {
            asset: string;
            clip?: string;
            clips?: Partial<Record<"walk"|"run"|"sprint", string>>;
            gain?: number;
        }>>;
    }
    export interface CharacterOptions {
        /** Optional when the project renders an existing scene Pawn itself. */
        resources?: CharacterResources;
        prefab?: string;
        entityName?: string;
        renderedByProject?: boolean;
        firstPersonViewmodel?: {
            forward?:number;drop?:number;right?:number;
            armPitch?:number;ads?:boolean;
        };
        footsteps?: CharacterFootstepOptions;
        /** Grounded locomotion reference speeds in metres per second. */
        locomotion?: {
            walkSpeed?:number;runSpeed?:number;sprintSpeed?:number;
            walkToRun?:number;runToSprint?:number;
            movementThreshold?:number;hysteresis?:number;
            smoothingSeconds?:number;
            minPlaybackRate?:number;maxPlaybackRate?:number;
        };
        states?: CharacterStates;
        tuning?: CharacterTuning;
        initialHeading?: number;
        hideInFirstPerson?: boolean;
        selectAnimation?: (context: CharacterAnimationContext) => string;
    }

    export interface CharacterController {
        start(sceneIndex: number): void;
        update(deltaTime: number): void;
        stop(): void;
        state(): Readonly<{
            active: boolean;
            entityIndex: number;
            animation: string;
            heading: number;
            elapsed: number;
        }>;
    }

    export class Character {
        static createController(options: CharacterOptions): CharacterController;
        static readonly Controller:
            (new (options: CharacterOptions) => CharacterController) | null;
    }

    export interface PlayerConfiguration {
        enabled: boolean;
        eyeHeight?: number;
        character?: CharacterOptions | null;
        /** Player gameplay profile: the shared weapon controller validates
         *  its additional muzzle/ejection socket requirements at startup. */
        weapon?: {enabled:false} | (Partial<WeaponProfile> & {
            enabled:true;prefab:string;magazineSize:number;
            reserveAmmo:number;roundsPerSecond:number;
            reloadSeconds:number;range:number;sweepRadius:number;
        });
        camera?: CameraOptions;
        look?: {
            mouseSensitivity?: number;
            /** RMB fine-aim multiplier in first person, 0.1..1.0. */
            adsSensitivityMultiplier?: number;
            maxPitch?: number;
            gamepadYawSpeed?: number;
            gamepadPitchSpeed?: number;
            aimOnRightMouse?: boolean;
        };
        movement?: {jumpKey?: number; sprintKey?: number;
                    walkKey?: number; walkScale?: number};
        /** Backward-compatible alias. Prefer character.footsteps. */
        footsteps?: CharacterFootstepOptions;
        interaction?: {
            enabled?: boolean;
            useKey?: number;
            range?: number;
            radius?: number;
            impulse?: number;
        };
        session?: {
            pauseKey?: number;
            exitKey?: number;
            title?: string;
        };
    }

    export interface PlayerMotionState {
        readonly pawn:string;
        readonly state:"idle"|"moving"|"jumping"|"falling";
        readonly horizontalSpeed:number;
        readonly verticalSpeed:number;
        readonly fallDistance:number;
        readonly impactSpeed:number;
        readonly grounded:boolean;
        readonly moving:boolean;
        readonly landed:boolean;
    }
    export interface PlayerActionEvent extends Omit<PlayerMotionState,"landed"> {
        readonly previous:string;
    }
    export class Player {
        static configure(config: PlayerConfiguration): Readonly<PlayerConfiguration>;
        static readonly config: Readonly<PlayerConfiguration>;
        static readonly enabled: boolean;
        static readonly index: number;
        static readonly entityId: string | null;
        static readonly hasPawn: boolean;
        /** Throw a readable error if no native Pawn is possessed. */
        static requirePawn(): SceneEntity;
        static possess(id: string | number): void;
        static unpossess(): void;
        static sprint(active: boolean): void;
        static readonly paused: boolean;
        static setIndex(index: number): void;
        static setPaused(paused: boolean): void;
        static move(worldX: number, worldZ: number): void;
        /** Requested movement input, independent of externally moved platforms. */
        static motion(): {x: number; z: number};
        /** True physics-frame locomotion state; null before simulation. */
        static motionState(): PlayerMotionState | null;
        /** Native post-physics event, for example Player.on("land", callback). */
        static on(action: string, handler:(event: PlayerActionEvent)=>void):()=>boolean;
        static once(action: string, handler:(event: PlayerActionEvent)=>void):()=>boolean;
        /** Physics contact query. Surface uses the collider's authored tag. */
        static ground(): {
            grounded: boolean;
            surface: "dirt" | "grass" | "metal" | "mud" | "snow" |
                     "stone" | "water" | "wood";
            objectIndex?: number;
            materialId?: number;
            triangleIndex?: number;
        };
        static jump(): void;
        static eye(): Vector3;
        static setEyeHeight(height: number): void;
    }

    export interface SceneEntity {
        index: number;
        id?: string;
        parent?: number | string | null;
        name?: string;

        mass?: number;
        size?: number;
        position: Vector3;
    }

    /** Runtime-only box collider / optional VFS OBJ visual. */
    export interface SpawnOptions {
        name?: string;
        position: Vector3;
        size?: number;
        /** Native model visual scale, independent of the collision proxy. */
        scale?: Vector3;
        mass?: number;
        color?: number;
        asset?: string;
        layer?: string;
        parent?: Entity | string | null;
        collision?: boolean;
    }

    export class PrefabInstance {
        readonly root: Entity;
        readonly entities: readonly Entity[];
        readonly alive: boolean;
        destroy(): number;
    }

    export class Prefab {
        /** Instantiate supported Scene V3 .nprefab objects via VFS. */
        static instantiate(path: string, options?: {position?: Vector3; collision?: boolean; physics?: "authored"|"visual"}): PrefabInstance;
    }

    export class Entity {
        readonly id: string;
        readonly index: number;
        readonly alive: boolean;
        readonly snapshot: SceneEntity | null;
        readonly position: Vector3 | null;
        getComponent<T=unknown>(name:string):T;
        hasComponent(name:string):boolean;
        setComponent(name:string,value:Record<string,unknown>):this;
        setPosition(position: Vector3): this;
        setRotation(eulerDegrees: Vector3): this;
        setColor(rgb: number): this;
        applyImpulse(impulse: Vector3): this;
        destroy(): boolean;
    }

    export class AuthoredEntityHandle {
        private constructor();
        readonly id: string;
        readonly index: number;
        readonly alive: boolean;
        readonly snapshot: SceneEntity | null;
        readonly position: Vector3 | null;
        getComponent<T=unknown>(name:string):T;
        hasComponent(name:string):boolean;
        setComponent(name:string,value:Record<string,unknown>):this;
        setPosition(position: Vector3): this;
        setRotation(eulerDegrees: Vector3): this;
    }

    export class Scene {
        /** Scene indices remain stable; destroyed runtime slots appear as null. */
        static objects(start?: number, count?: number): (SceneEntity | null)[];
        static spawn(options: SpawnOptions): Entity;
        static objectCount(): number;
        static get(index: number): SceneEntity | null;
        static iterate(pageSize?: number): IterableIterator<SceneEntity>;
        static findByName(name: string): SceneEntity | null;
        static requireByName(name: string): SceneEntity;
        /** Reuse authored Entity: non-owning handle, no second prefab instance. */
        static handleByName(name: string): AuthoredEntityHandle | null;
        static handle(index: number): AuthoredEntityHandle | null;
        static require(index: number): SceneEntity;
        /** Canonical current Pawn, provided by native PlayerController. */
        static findPawn(): SceneEntity | null;
        /** Compatibility alias for findPawn(), not a serialized player flag. */
        static findPlayer(): SceneEntity | null;
        static nearby(center: Vector3, radius: number, maxResults?: number): SceneEntity[];
        static moveStatic(index: number, position: Vector3): void;
        static rotateStatic(index: number, eulerDegrees: Vector3): void;
        static setColor(index: number, rgb: number): void;
        /** Presentation-only: one persistent skinned entity across camera modes. */
        static setVisualMode(index: number,
            mode: "world" | "first_person_arms" | "first_person_body",
            settings?: { forward?: number; drop?: number; armPitch?: number; right?: number;
                ads?: boolean; recoilBack?: number; recoilArm?: number }): void;
        /** Attach a visual Entity to a named animated skeletal joint in the active frame. */
        static attachToBone(childIndex: number, parentIndex: number,
            bone: string, offset?: Vector3, rotation?: Vector3,
            alignment?: "socket" | "camera_forward"): void;
        /** Transform a weapon-local point by the last rendered skeletal attachment.
         *  Null means the socket has not been evaluated; no camera fallback. */
        static attachmentPoint(childIndex: number, localPoint: Vector3): Vector3 | null;
        /** Remove the runtime bone constraint without destroying the child. */
        static detachFromBone(childIndex: number): void;
        /** Animate native YDD/YDR geometry using an original YCD clip. */
        static playAnimation(index: number, clipReference: string, playbackRate?: number): void;
        /** Remove the runtime animation override, preserving authored data. */
        static stopAnimation(index: number): void;
    }

    export interface PhysicsHit {
        materialId?: number;
        triangleIndex?: number;
        index: number;
        fraction: number;
    }

    export class Collision {
        static sweepSphere(from:Vector3,to:Vector3,radius?:number,ignoreIndex?:number):PhysicsHit|null;
        static lineOfSight(from:Vector3,to:Vector3,
            options?:{radius?:number;ignoreIndex?:number}):boolean;
        /** Returns sphere center at contact, not actual collider contact. */
        static traceSphere(from:Vector3,to:Vector3,radius?:number,ignoreIndex?:number):
            Readonly<{hit:PhysicsHit;point:Vector3}>|null;
    }
    export interface ParticleEmission {position:Vector3;direction?:Vector3;scale?:number}
    export class Particles {
        static available(effect?:string):boolean;
        static emit(effect:string,options:ParticleEmission):boolean;
        static tryEmit(effect:string,options?:ParticleEmission):boolean;
    }
    export interface PhysicsContactEvent {
        readonly first:number;
        readonly second:number;
        readonly phase:number;
        readonly response:number;
    }
    export class Physics {
        static sweepSphere(from: Vector3, to: Vector3,
            radius: number, ignoreIndex?: number): PhysicsHit | null;
        static applyImpulse(index: number, impulse: Vector3): void;
        /** Jolt/compatible provider optional body friction and bounce coefficients. */
        static setBodyMaterial(index:number,material?:{
            friction?:number;restitution?:number}):boolean;
        /** Drains physics contacts: -1 is native ground, 0=Begin/1=End. */
        /** Subscribe to a single globally-drained native contact stream. */
        static onContacts(callback:(events:readonly PhysicsContactEvent[],
            dt:number)=>void):()=>boolean;
        static contacts():readonly PhysicsContactEvent[];
        static readonly contactEpoch:number;
        static resetContacts():void;
        /** Legacy manual poll; reads snapshot while dispatcher is subscribed. */
        static pollContacts(max?:number):ReadonlyArray<PhysicsContactEvent>;
    }

    /** Physics-authoritative two-stage shot: camera target followed by an
     *  actual muzzle-origin sweep. Used by any project-defined firearm. */
    export class WeaponBallistics {
        static solve(options: {
            weaponIndex: number;
            muzzleLocal: Vector3;
            /** Ejection port; if absent, no physical casing origin is assumed. */
            ejectionLocal?: Vector3;
            barrelAxisLocal?: Vector3;
            /** auto: sight for first-person hip fire and ADS; barrel for other modes. */
            mode?: "auto" | "sight" | "barrel";
            eye?: Vector3;
            aimDirection?: Vector3;
            range: number;
            radius: number;
            adsRadius?: number;
            aimRadius?: number;
            ignoreIndex?: number;
        }): {
            muzzle: Vector3;
            aimPoint: Vector3;
            /** Muzzle-origin shot direction converging on the camera target in FPS. */
            direction: Vector3;
            /** Actual current barrel orientation from the weapon transform. */
            barrelDirection: Vector3;
            mode: "sight" | "barrel";
            casing: {origin: Vector3;right: Vector3;up: Vector3} | null;
            effects: {
                flash: {origin: Vector3;direction: Vector3};
                smoke: {origin: Vector3;direction: Vector3};
                casing: {origin: Vector3;right: Vector3;up: Vector3} | null;
            };
            hit: PhysicsHit | null;
            blocked: boolean;
            cameraRay: {from: Vector3;to: Vector3};
            shotRay: {from: Vector3;to: Vector3};
        } | null;
    }

    /** Shared runtime service for every firearm; particles are short-lived,
     *  casing prefabs are real Jolt bodies without a despawn timer. */
    export interface WeaponRecoil {
        amplitude?: number; duration?: number;
        pitchRadians?: number; yawRadians?: number; recoverySeconds?: number;
        /** Physical viewmodel snap along sight line, metres. */
        weaponBackMeters?: number;
        /** Additive arm/elbow impulse, normalized 0..1. */
        armImpulse?: number;
        snapSeconds?: number; accentSeconds?: number;
        settleSeconds?: number; aftershock?: number;
    }
    /** Optional ADS-specific camera kick; keeps weapon profiles data-driven. */
    export interface WeaponRecoilProfile extends WeaponRecoil {
        aimDownSights?: WeaponRecoil;
        shotPattern?: Array<{pitchScale:number;yawScale:number}>;
        patternYawRadians?: number;
    }
    export class WeaponEffects {
        /** Process real rigid-body collision events and play shell NAC cues. */
        static update(dt:number):void;
        static advanceMotion(dt:number): Readonly<{backMeters:number;armImpulse:number;active:boolean}>;
        static viewMotion(): Readonly<{backMeters:number;armImpulse:number;active:boolean}>;
        static resetMotion():void;
        static kick(options?: WeaponRecoil): Readonly<{shake:boolean;rotation:boolean}>;
        static fire(options: {
            shot: {
                muzzle: Vector3;
                barrelDirection: Vector3;
                casing: {origin: Vector3;right: Vector3;up: Vector3} | null;
            };
            casingType?: "pistol" | "rifle";
            casingPrefab?: string | null;
            shellImpulse?: number;
            casingAudio?: string | null;
            casingMaterial?: {friction?:number;restitution?:number};
            emitCasing?: boolean;
            recoil?: WeaponRecoil | null;
        }): Readonly<{
            particles: boolean;
            casing: {readonly root: Entity;readonly alive: boolean} | null;
            serial: number;
            casingFailed: boolean;
        }>;
        static stats(): Readonly<{
            shots:number;casingFailures:number;trackedShells:number;
            contactSounds:number;rollSounds:number}>;
    }

    export interface WeaponProfile {
        enabled: boolean; prefab: string; displayName?: string;
        grip?: {bone:string;offset?:Vector3;rotation?:Vector3;
            firstPersonRotation?:Vector3;
            aimDownSights?:{
                forward?:number;drop?:number;right?:number;armPitch?:number;
            }};
        muzzleLocal: Vector3; ejectionLocal?: Vector3; barrelAxisLocal?:Vector3;
        magazineSize:number;reserveAmmo:number;roundsPerSecond:number;
        reloadSeconds:number;range:number;sweepRadius:number;aimSweepRadius?:number;
        fireMode?:"semi"|"auto";recoil?:WeaponRecoilProfile;
        animations?: {dictionary:string;idle?:string;fire?:string;reload?:string;empty?:string};
        audio?: string;casingType?:"pistol"|"rifle";casingPrefab?:string;
        emitCasing?: boolean; shellImpulse?:number;fireGain?:number;
        casingAudio?:string;
        casingMaterial?:{friction?:number;restitution?:number};
        hudAsset?:string;hudLayer?:string;impactImpulse?:number;fireAnimationSeconds?:number;
        /** For unheld or AI-owned weapons, this is the spawn anchor in world metres. */
        position?:Vector3;
        cameraRecoil?:boolean;
        controlHint?:string;
    }
    export class WeaponController extends Script {
        constructor(options?: {
            profile?: (()=>WeaponProfile|null|undefined)|null;
            holder?: (()=>PrefabInstance|Entity|AuthoredEntityHandle|null|undefined)|null;
            /** Explicit input source for NPC/AI, no keyboard read is performed. */
            controls?: (()=>{trigger:boolean;reload:boolean})|null;
            aim?: (()=>{eye?:Vector3;direction?:Vector3;mode?:"auto"|"sight"|"barrel"})|null;
            onHit?: ((event:{hit:PhysicsHit;shot:unknown;target:SceneEntity|null;
                      weapon:WeaponController})=>void)|null;
        });
        readonly equipped:boolean;readonly ammo:number;readonly reserveAmmo:number;
        readonly reloading:boolean;readonly status:string;readonly weaponIndex:number;
        reload():boolean;
        /** Explicit scripted shot with shared physics, VFX and ammunition. */
        fire():boolean;
    }
    export class Session {
        static pause(paused: boolean): void;
        static quit(): void;
        static restart(): void;
        /** Currently loaded project-relative scene path. */
        static scene(): string;
        /** Deferred transition to a project-owned .nscn scene. */
        static loadScene(path: string): boolean;
    }

    export type UiValue = string | number | boolean;
    /** Native document properties; custom project/provider keys remain valid. */
    export interface UiProperties {
        x?: number; y?: number; width?: number; height?: number;
        padding?: number; gap?: number; radius?: number;
        color?: string; background?: string; border?: string;
        text?: string; source?: string; opacity?: number;
        font_scale?: number; font?: string;
        visible?: boolean; enabled?: boolean; primary?: boolean;
        position?: string; fit?: string;
        [name: string]: UiValue | undefined;
    }
    export type UiEventName = "click" | "change" | "focus" | "blur" |
                              "key" | "pointer" | "press";
    /** Data-only scene-independent UI descriptor. No JSON or native handles. */
    export interface UiDeclaration {
        id: string;
        type: "panel" | "text" | "button" | "image" | "input";
        props?: UiProperties;
        text?: string;
        caption?: string;
        source?: string;
        parent?: string;
        children?: UiDeclaration[];
        events?: Partial<Record<UiEventName,(event:UiEvent)=>void>>;
    }
    export interface UiDocument {
        nodes: UiDeclaration[];
    }
    export interface UiNode { id: string; type: UiDeclaration["type"];
        parent?: string; props?: Record<string, UiValue>; }
    /** Scoped authoring helper: child IDs inherit the group's parent. */
    export interface UIGroup {
        panel(id: string, props?: Record<string,string | number | boolean>): this;
        text(id: string, content: string, props?: Record<string,string | number | boolean>): this;
        button(id: string, caption: string, props?: Record<string,string | number | boolean>): this;
        image(id: string, source: string, props?: Record<string,string | number | boolean>): this;
        input(id: string, props?: Record<string,string | number | boolean>): this;
        progress(id: string, fraction: number, props?: Record<string,string | number | boolean>): this;
        group(id: string, props: Record<string,string | number | boolean>, build: (children:UIGroup)=>void): this;
    }
    export interface UiEvent { id: string; name: string; kind: number; value: string | number | boolean | null; }
    export interface WorldUiOptions {
        layer?: string; width?: number; height?: number;
        worldWidth?: number; worldHeight?: number;
    }
    export interface UIRoot { readonly name: string; readonly mounted: boolean;
        readonly target: "screen" | "world";
        readonly size: number;
        has(id: string): boolean;
        snapshot(): UiNode[];
        /** Atomically reconcile a typed nested tree without duplicate GPU uploads. */
        render(
            document: UiDeclaration | UiDeclaration[] | UiDocument,
            options?: {preserveRuntimeOverrides?: boolean}
        ): this;
        on(id: string, event: UiEventName, callback: (event:UiEvent)=>void): this;
        onClick(id: string, callback: (event:UiEvent)=>void): this;
        onPointer(id: string, callback: (event:UiEvent)=>void): this;
        onPress(id: string, callback: (event:UiEvent)=>void): this;
        onChange(id: string, callback: (event:UiEvent)=>void): this;
        off(id: string, event: string, callback: (event:UiEvent)=>void): this;
        group(id: string, props?: Record<string,string | number | boolean>, build?: ((children:UIGroup)=>void) | null, parent?: string): this;
        panel(id: string, props?: Record<string,string | number | boolean>, parent?: string): this;
        text(id: string, content: string, props?: Record<string,string | number | boolean>, parent?: string): this;
        button(id: string, caption: string, props?: Record<string,string | number | boolean>, parent?: string): this;
        image(id: string, source: string, props?: Record<string,string | number | boolean>, parent?: string): this;
        input(id: string, props?: Record<string,string | number | boolean>, parent?: string): this;
        progress(id: string, fraction: number, props?: Record<string,string | number | boolean>, parent?: string): this;
        set(id: string, props: Record<string,string | number | boolean>): this;
        batch(callback: (root: this) => void): this;
        remove(id: string): this; clear(): this;
        mount(): this; unmount(): this; dispose(): void;
        setTransient(id: string, changes: Record<string, string | boolean | number>): this;
        setText(id: string, text: string): this; setProgress(id: string, fraction: number): this; }
    export class UI {
        static setTitle(title: string): void;
        static createRoot(layer?: string): UIRoot;
        static createSurface(entityId: string | number, options?: WorldUiOptions): UIRoot;
        /** Create an editable UI root from a .nui asset mounted by the shared/project VFS. */
        static loadAsset(path: string, options?: WorldUiOptions & {entityId?: string | number}): UIRoot;
        /** Manually check loaded .nui files and update live roots; automatically polled in development. */
        static reloadChangedAssets(): number;
        /** Advance .nui numeric animation tracks by one frame delta (seconds). */
        static advanceAnimations(dt: number): void;
        static playAssetAnimation(root: UIRoot, name: string): boolean;
        static hasSurface(entityId: string | number): boolean;
        static mountSurface(entityId: string | number, layer: string, elements: UiNode[], options?: WorldUiOptions): void;
        static unmountSurface(entityId: string | number): void;
        static hasLayer(layer: string): boolean;
        static mount(layer: string, elements: UiNode[]): void;
        static unmount(layer: string): void;
        static clear(): void;
    }

    export interface VehiclePose {
        index: number; x: number; y: number; z: number;
        heading: number; speed: number; waypointActive: boolean;
    }
    export interface VehicleMotion {
        forwardSpeed: number; lateralSpeed: number; yawRate: number;
        longitudinalSlip: number; lateralSlip: number;
        distance: number; groundedWheels: number;
    }
    export interface VehiclePowertrainState {
        /** -1 = reverse, 0 = neutral, 1..8 = forward ratio. */
        gear: number;
        /** Engine revolutions per minute. */
        rpm: number;
        torqueNm: number;
        wheelForceN: number;
        shiftingSeconds: number;
    }
    /** Provider-neutral engine.vehicle API. Dynamics and cab geometry are
     * configured with project-owned .nvehicle XML, never in game scripts. */
    export interface VehicleControls {throttle?:number;steer?:number;brake?:number}
    export interface VehicleDestination {x:number;z:number;speed:number;radius?:number}
    export interface VehicleHandle {
        readonly index:number;readonly state:VehiclePose;readonly motion:VehicleMotion;
        readonly powertrain:VehiclePowertrainState;
        drive(options:VehicleControls):boolean;
        navigate(destination:VehicleDestination):boolean;
        selectGear(gear:number):boolean;stop():boolean;detach():boolean;
    }
    export interface VehicleDoorState {
        fraction:number;
        [field:string]:number|boolean;
    }
    export class Vehicle {
        static available():boolean;
        static handle(index:number):VehicleHandle;
        static drive(index:number,options:VehicleControls):boolean;
        static navigate(index:number,destination:VehicleDestination):boolean;
        static attach(index:number,wheelbase:number,maxSpeed:number):boolean;
        static configureRig(index:number,path:string):boolean;
        static controls(index:number,throttle:number,steering:number,brake:number):boolean;
        static waypoint(index:number,x:number,z:number,speed:number,radius?:number):boolean;
        static state(index:number):VehiclePose;
        static motion(index:number):VehicleMotion;
        static powertrainState(index:number):VehiclePowertrainState;
        static selectGear(index:number,gear:number):boolean;
        static wheelState(index:number,wheel:number):Record<string,number|boolean>;
        static wheelPose(index:number,wheel:number):Record<string,number|boolean>;
        static wheelCount(index:number):number;
        static seatCount(index:number):number;
        static seatState(index:number,seat:number):Record<string,number|boolean>;
        static occupy(index:number,seat:number,entityId:number):boolean;
        static release(index:number,seat:number,entityId:number):boolean;
        static setDoor(index:number,door:number,open:boolean):boolean;
        static doorCount(index:number):number;
        static doorState(index:number,door:number):VehicleDoorState;
        static detach(index:number):boolean;
        static provider():string;
        static count():number;
    }

    /** Shared creation and acquisition facade: existing native providers only. */
    export interface EngineFactory {
        entity(descriptor:SpawnOptions):Entity;
        prefab(path:string,options?:{position?:Vector3;collision?:boolean;physics?:"authored"|"visual"}):PrefabInstance;
        existing(name:string,required?:boolean):AuthoredEntityHandle|null;
        vehicle(index:number):VehicleHandle;
        weapon(options?:ConstructorParameters<typeof WeaponController>[0]):WeaponController;
        camera(options?:CameraOptions):CameraViewController;
        character(options?:CharacterOptions):CharacterController;
        script<T extends Script>(Type:new (options?:any)=>T,options?:Record<string,unknown>):T;
        entityScript<T extends EntityScript>(
            Type:new (options:{entity:Entity | AuthoredEntityHandle;owner?:unknown})=>T,
            entity:Entity | AuthoredEntityHandle,options?:Record<string,unknown>):T;
        create(kind:"entity",descriptor:SpawnOptions):Entity;
        create(kind:"prefab",descriptor:string|{path:string;options?:{position?:Vector3;collision?:boolean;physics?:"authored"|"visual"}}):PrefabInstance;
        create(kind:"existing",descriptor:string|{name:string;required?:boolean}):AuthoredEntityHandle|null;
        create(kind:"vehicle",descriptor:number):VehicleHandle;
        create(kind:"weapon",descriptor?:ConstructorParameters<typeof WeaponController>[0]):WeaponController;
        create(kind:"camera",descriptor?:CameraOptions):CameraViewController;
        create(kind:"character",descriptor?:CharacterOptions):CharacterController;
        create(kind:string,descriptor?:unknown):unknown;
        register(kind:string,creator:(descriptor:unknown,api:EngineAPI)=>unknown):Readonly<{dispose():boolean}>;
        kinds():readonly string[];
    }
    export const Factory:EngineFactory;
    export class EngineAPI {
        private constructor();
        readonly Factory:EngineFactory;
        readonly entity:typeof Scene;
        readonly prefab:typeof Prefab;
        readonly vehicle:typeof Vehicle;
        readonly weapon:typeof WeaponController;
        readonly ballistics:typeof WeaponBallistics;
        readonly weaponEffects:typeof WeaponEffects;
        readonly camera:typeof Camera;
        readonly physics:typeof Physics;
        readonly collision:typeof Collision;
        readonly particles:typeof Particles;
        readonly character:typeof Character;
        readonly player:typeof Player;
        readonly events:typeof Events;
        readonly input:typeof Input;
        readonly audio:typeof Audio;
        readonly audioVoice:typeof AudioVoice;
        readonly ui:typeof UI;
        readonly session:typeof Session;
        readonly logger:typeof Logger;
        readonly ecs:typeof ECS;
        readonly time:typeof Time;
        readonly scheduler:typeof Scheduler;
        readonly assets:typeof Assets;
        readonly scripts:typeof Script;
        readonly capabilities:typeof Capabilities;
        readonly CapabilityError:typeof EngineCapabilityError;
        readonly storage:typeof Storage;
        /** All renderer services are grouped under one canonical namespace. */
        readonly render:typeof Render;
    }
    export const API:Readonly<EngineAPI>;

    export class Storage {
        static read(key:string):string | null;
        static write(key:string,value:string):boolean;
    }
    export interface SkySettings {
        /** Project-authored source selection, shared by Editor and Player. */
        mode?:"disabled"|"procedural"|"drawable"|"hybrid";
        drawable?:string; // VFS asset reference, e.g. worlds/sky.ydd@layer
        preset?:string;
        enabled?:boolean;paused?:boolean;
        sun?:boolean;moon?:boolean;stars?:boolean;clouds?:boolean;
        /** Local solar time, 0 <= timeOfDay < 24. */
        timeOfDay?:number;
        /** Duration of one simulated day, in real seconds. */
        dayLengthSeconds?:number;
        latitudeDegrees?:number;
        dayOfYear?:number;
        moonPhase?:number;
        cloudCoverage?:number;cloudSpeed?:number;
        haze?:number; fogDensity?:number;
        sunIntensity?:number;moonIntensity?:number;starDensity?:number;
    }
    export interface SkyState extends Required<SkySettings> {
        readonly sunDirection:Readonly<Vector3>;
        readonly moonDirection:Readonly<Vector3>;
        readonly sunElevationDegrees:number;
        readonly moonElevationDegrees:number;
        readonly moonIllumination:number;
        readonly daylight:number;
        readonly cloudOffset:number;
        readonly revision:number;
    }
    /** Built-in atmosphere provider, works with Vulkan/D3D12 sky meshes. */
    export class Sky {
        static configure(settings?:SkySettings):Readonly<SkyState>;
        static state():Readonly<SkyState>;
        static enable(value?:boolean):Readonly<SkyState>;
        static setTimeOfDay(hours:number,minutes?:number):Readonly<SkyState>;
        static pause(paused?:boolean):Readonly<SkyState>;
        static setCloudCoverage(coverage:number):Readonly<SkyState>;
        /** Set cloud coverage 0..1 or change enabled, coverage and UV speed. */
        static setClouds(settings?:number|{enabled?:boolean;coverage?:number;speed?:number}):Readonly<SkyState>;
        static setSun(settings?:boolean|{enabled?:boolean;intensity?:number}):Readonly<SkyState>;
        static setMoon(settings?:boolean|{enabled?:boolean;intensity?:number;phase?:number}):Readonly<SkyState>;
        static setStars(settings?:boolean|{enabled?:boolean;density?:number}):Readonly<SkyState>;
        static setDayLength(seconds:number):Readonly<SkyState>;
        static advance(seconds:number):Readonly<SkyState>;
        static reset():Readonly<SkyState>;
        static get timeOfDay():number;
        static get sunDirection():Readonly<Vector3>;
        static get moonDirection():Readonly<Vector3>;
        static get daylight():number;
    }
    export interface ShaderCatalogEntry {
        readonly path:string;
        readonly language:"hlsl"|"glsl";
        readonly system:string;
    }
    export interface ShaderCatalog {
        readonly provider:string|null;
        readonly hotReload:boolean;
        readonly builtin:readonly ShaderCatalogEntry[];
    }
    export interface RenderShaderAsset {
        readonly path:string;
        readonly language:"hlsl"|"glsl";
        readonly source:string;
    }
    export class RenderShaders {
        static catalog():Readonly<ShaderCatalog>;
        static source(path:string):string;
        static load(path:string):Readonly<RenderShaderAsset>;
        /** Queue an already compiled project material shader (.spv or .dxil). */
        static queueCompiled(sourcePath:string,binaryPath:string):number;
        static poll():Readonly<{pending:boolean;completed:readonly string[]}>;
        static get supportsHotReload():boolean;
    }
    export interface RenderShadowsState {
        readonly provider:string|null;
        readonly requested:boolean;
        readonly filter:"pcss"|"pcf"|"off";
        readonly cascades:number;
        readonly verifiedOnGpu:boolean;
        readonly requiresProviderRestart:boolean;
    }
    export interface RenderShadowCapabilities {
        readonly csm:boolean;
        readonly pcf:boolean;
        readonly pcss:boolean;
        readonly runtimeToggle:boolean;
    }
    export class RenderShadows {
        static state():Readonly<RenderShadowsState>;
        static capabilities():Readonly<RenderShadowCapabilities>;
    }
    export class RenderProvider {
        static name():string|null;
        static providerVersion():string|null;
        static version():string;
        static setDetailLevel(level:"balanced"|"high"|"ultra"|"cinematic"):unknown;
        static detailLevel():string;
    }
    export class Render {
        static readonly provider:typeof RenderProvider;
        static readonly shaders:typeof RenderShaders;
        static readonly shadows:typeof RenderShadows;
        static readonly sky:typeof Sky;
        static version():string;
        static setDetailLevel(level:"balanced"|"high"|"ultra"|"cinematic"):unknown;
        static detailLevel():string;
    }
    export class Capabilities {
        private constructor();
        static has(name:string):boolean;
        static require(name:string):boolean;
        static list():Readonly<Record<string,boolean>>;
    }
    export class EngineCapabilityError extends Error {
        readonly code:"CAPABILITY_UNAVAILABLE";
        readonly capability:string;
    }
    export class Logger {
        static debug(message: string): void;
        static info(message: string): void;
        static warn(message: string): void;
        static error(message: string): void;
    }

    export interface ScriptOwner {
        readonly id?:string;
        readonly index?:number;
    }
    export class Time {
        private constructor();
        static readonly delta:number;
        static readonly elapsed:number;
        static readonly frame:number;
    }
    export class ScheduledTask {
        private constructor();
        cancel():boolean;
    }
    export class Scheduler {
        constructor(onError?:(error:unknown)=>void);
        after(seconds:number,callback:()=>void):ScheduledTask;
        every(seconds:number,callback:()=>void):ScheduledTask;
        update(dt:number):void;
        readonly pending:number;
        dispose():boolean;
    }
    export class ScriptContext {
        constructor(options?:{entity?:Entity | AuthoredEntityHandle | null;
            owner?:ScriptOwner | null});
        readonly entity:Entity | AuthoredEntityHandle | null;
        readonly owner:ScriptOwner | null;
        readonly active:boolean;
        readonly scheduler:Scheduler;
        own<T extends {dispose?():unknown;cancel?():unknown;
            uninstall?():unknown;destroy?():unknown} | (()=>unknown)>(resource:T):T;
        listen(name:string,handler:(payload:any)=>void):()=>boolean;
        after(seconds:number,callback:()=>void):ScheduledTask;
        every(seconds:number,callback:()=>void):ScheduledTask;
        tick(dt:number):void;
        dispose():boolean;
    }
    export class ECS {
        static available(operation?:string):boolean;
        static has(entity:string | Entity | AuthoredEntityHandle,name:string):boolean;
        static get<T=unknown>(entity:string | Entity | AuthoredEntityHandle,name:string):T;
        static set(entity:string | Entity | AuthoredEntityHandle,
            name:string,value:Record<string,unknown>):unknown;
        static remove(entity:string | Entity | AuthoredEntityHandle,name:string):unknown;
        static query(required:string[]):unknown;
    }
    export class Assets {
        static available(kind:string):boolean;
        static prefab(path:string,options?:{position?:Vector3;collision?:boolean;physics?:"authored"|"visual"}):PrefabInstance;
        static ui(path:string,options?:WorldUiOptions):UIRoot;
        static open(path:string,kind:string,options?:Record<string,unknown>):unknown;
    }

    export abstract class PlayerScript extends Script {
        readonly enabled: boolean;
        readonly active: boolean;
        readonly playing: boolean;
        readonly options: Readonly<PlayerConfiguration> | null;
    }

    export abstract class Script {
        constructor(options?:{entity?:Entity | AuthoredEntityHandle | null;
            owner?:ScriptOwner | null});
        readonly installed:boolean;
        readonly context:ScriptContext | null;
        readonly entity:Entity | AuthoredEntityHandle | null;
        listen(name:string,handler:(payload:any)=>void):()=>boolean;
        own<T extends {dispose?():unknown;cancel?():unknown;
            uninstall?():unknown;destroy?():unknown} | (()=>unknown)>(resource:T):T;
        after(seconds:number,callback:()=>void):ScheduledTask;
        every(seconds:number,callback:()=>void):ScheduledTask;
        uninstall():boolean;
        install():this;
        start():void;
        update(deltaTime:number):void;
        stop():void;
        onDestroy():void;
    }

    export abstract class EntityScript extends Script {
        constructor(options:{entity:Entity | AuthoredEntityHandle;owner?:ScriptOwner});
        readonly entity:Entity | AuthoredEntityHandle;
        onStart():void;
        onUpdate(dt:number):void;
        onStop():void;
    }
    export abstract class ComponentScript extends EntityScript {
        constructor(options:{entity:Entity | AuthoredEntityHandle;
            component:string;owner?:ScriptOwner});
        readonly componentName:string;
        readonly component:unknown;
    }
}
