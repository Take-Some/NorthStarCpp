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
        /** Teleport/shot cut: clear follow interpolation but preserve active track time. */
        static cut(): void;
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

    export class Audio {
        static playWav(path: string): number;
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
        idle?: string;
        walking?: string;
        jumping?: string;
        falling?: string;
    }

    export interface CharacterTuning {
        walkSpeed?: number;
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

    export interface CharacterOptions {
        resources: CharacterResources;
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
        camera?: CameraOptions;
        look?: {
            mouseSensitivity?: number;
            maxPitch?: number;
            gamepadYawSpeed?: number;
            gamepadPitchSpeed?: number;
            aimOnRightMouse?: boolean;
        };
        movement?: {jumpKey?: number; sprintKey?: number};
        footsteps?: {
            enabled?: boolean;
            walkStride?: number;
            runStride?: number;
            sprintStride?: number;
            runSpeed?: number;
            sprintSpeed?: number;
        };
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
        static instantiate(path: string, options?: {position?: Vector3}): PrefabInstance;
    }

    export class Entity {
        readonly id: string;
        readonly index: number;
        readonly alive: boolean;
        readonly snapshot: SceneEntity | null;
        setPosition(position: Vector3): this;
        setRotation(eulerDegrees: Vector3): this;
        setColor(rgb: number): this;
        applyImpulse(impulse: Vector3): this;
        destroy(): boolean;
    }

    export interface AuthoredEntityHandle {
        readonly id: string;
        readonly index: number;
        readonly alive: boolean;
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
        /** Existing authored Entity handle; does not spawn a new prefab. */
        static handleByName(name: string): AuthoredEntityHandle | null;
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
            mode: "world" | "first_person_arms",
            settings?: { forward?: number; drop?: number; armPitch?: number }): void;
        /** Attach a visual Entity to a named animated skeletal joint in the active frame. */
        static attachToBone(childIndex: number, parentIndex: number,
            bone: string, offset?: Vector3, rotation?: Vector3,
            alignment?: "socket" | "camera_forward"): void;
        /** Remove the runtime bone constraint without destroying the child. */
        static detachFromBone(childIndex: number): void;
        /** Animate native YDD/YDR geometry using an original YCD clip. */
        static playAnimation(index: number, clipReference: string): void;
        /** Remove the runtime animation override, preserving authored data. */
        static stopAnimation(index: number): void;
    }

    export interface PhysicsHit {
        materialId?: number;
        triangleIndex?: number;
        index: number;
        fraction: number;
    }

    export class Physics {
        static sweepSphere(from: Vector3, to: Vector3,
            radius: number, ignoreIndex?: number): PhysicsHit | null;
        static applyImpulse(index: number, impulse: Vector3): void;
    }

    export class Session {
        static pause(paused: boolean): void;
        static quit(): void;
        static restart(): void;
    }

    export interface UiNode { id: string; type: "panel" | "text" | "button" | "image" | "input";
        parent?: string; props?: Record<string, string | number | boolean>; }
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
        on(id: string, event: "click" | "change" | "focus" | "blur" | "key" | "pointer", callback: (event:UiEvent)=>void): this;
        onClick(id: string, callback: (event:UiEvent)=>void): this;
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
        setText(id: string, text: string): this; setProgress(id: string, fraction: number): this; }
    export class UI {
        static setTitle(title: string): void;
        static createRoot(layer?: string): UIRoot;
        static createSurface(entityId: string | number, options?: WorldUiOptions): UIRoot;
        static hasSurface(entityId: string | number): boolean;
        static mountSurface(entityId: string | number, layer: string, elements: UiNode[], options?: WorldUiOptions): void;
        static unmountSurface(entityId: string | number): void;
        static hasLayer(layer: string): boolean;
        static mount(layer: string, elements: UiNode[]): void;
        static unmount(layer: string): void;
        static clear(): void;
    }

    export class Logger {
        static debug(message: string): void;
        static info(message: string): void;
        static warn(message: string): void;
        static error(message: string): void;
    }

    export abstract class PlayerScript extends Script {
        readonly enabled: boolean;
        readonly active: boolean;
        readonly playing: boolean;
        readonly options: Readonly<PlayerConfiguration> | null;
    }

    export abstract class Script {
        readonly installed: boolean;
        uninstall(): boolean;
        install(): this;
        start(): void;
        update(deltaTime: number): void;
        stop(): void;
    }
}
