// NorthStarCpp shared scripting SDK: class-based, per-context services.
// Loads before scripts/character/Character.js, scripts/camera/playerCamera.js and project scripts.
// Only thin engine API contracts are required; gameplay remains project-owned.
(() => {
    "use strict";

    if (Object.prototype.hasOwnProperty.call(globalThis, "NorthStar"))
        throw new Error("NorthStar shared script API already registered");

    class CameraViewController {
        #toggleKey;
        #transitionSeconds;
        #initialMode;
        #minDistance;
        #maxDistance;
        #wheelStep;
        #previousKeyDown = false;
        #followDistance = null;
        #started = false;

        constructor(options = {}) {
            this.#toggleKey = options.toggleKey === undefined ? 86 : options.toggleKey;
            this.#transitionSeconds = options.transitionSeconds === undefined
                ? 0.45 : options.transitionSeconds;
            this.#initialMode = options.initialMode || "first_person";
            this.#minDistance = options.minDistance === undefined ? 1.5 : options.minDistance;
            this.#maxDistance = options.maxDistance === undefined ? 12 : options.maxDistance;
            this.#wheelStep = options.wheelStep === undefined ? 0.7 : options.wheelStep;

            if (!Number.isInteger(this.#toggleKey) || this.#toggleKey < 0 ||
                this.#toggleKey > 255)
                throw new RangeError("Camera toggle key must be a virtual-key code (0..255)");
            this.#validateDuration(this.#transitionSeconds);
            if (!CameraViewController.MODES.includes(this.#initialMode))
                throw new RangeError("Invalid initial camera view");
            if (!Number.isFinite(this.#minDistance) || this.#minDistance < 0.5 ||
                !Number.isFinite(this.#maxDistance) ||
                this.#maxDistance <= this.#minDistance || this.#maxDistance > 100)
                throw new RangeError("Camera follow distance must be within 0.5..100 metres");
            if (!Number.isFinite(this.#wheelStep) || this.#wheelStep <= 0 ||
                this.#wheelStep > 10)
                throw new RangeError("Camera wheel step must be within (0, 10] metres");

            Object.freeze(this);
        }

        static get MODES() { return ["first_person", "third_person"]; }

        #validateDuration(value) {
            if (!Number.isFinite(value) || value < 0 || value > 10)
                throw new RangeError("Camera transition duration must be 0..10 seconds");
        }

        #clamp(value) {
            return Math.max(this.#minDistance, Math.min(this.#maxDistance, value));
        }

        #initializeDistance() {
            const current = engine.camera.followDistance();
            if (!Number.isFinite(current))
                throw new RangeError("Camera follow distance is not finite");
            this.#followDistance = this.#clamp(current);
            if (this.#followDistance !== current)
                engine.camera.setFollowDistance(this.#followDistance);
        }

        start() {
            this.#initializeDistance();
            // A key held during startup must not immediately reverse the view.
            this.#previousKeyDown = engine.input.isDown(this.#toggleKey);
            engine.camera.setMode(this.#initialMode, 0);
            this.#started = true;
        }

        stop() {
            this.#started = false;
            this.#previousKeyDown = false;
        }

        mode() { return engine.camera.mode(); }

        distance() {
            if (this.#followDistance === null) this.#initializeDistance();
            return this.#followDistance;
        }

        setMode(mode, duration = this.#transitionSeconds) {
            if (!CameraViewController.MODES.includes(mode))
                throw new RangeError("Unknown camera view");
            this.#validateDuration(duration);
            engine.camera.setMode(mode, duration);
            return mode;
        }

        toggle() {
            return this.setMode(this.mode() === "first_person"
                ? "third_person" : "first_person");
        }

        update() {
            if (!this.#started) return;
            const down = engine.input.isDown(this.#toggleKey);
            if (down && !this.#previousKeyDown) this.toggle();
            this.#previousKeyDown = down;

            // Wheel units are signed, fractional notches. Positive is closer.
            const steps = engine.input.wheelDelta();
            if (!Number.isFinite(steps))
                throw new RangeError("Non-finite camera wheel input");
            if (this.mode() !== "third_person" || steps === 0) return;
            if (this.#followDistance === null) this.#initializeDistance();
            const next = this.#clamp(this.#followDistance - steps * this.#wheelStep);
            if (next !== this.#followDistance) {
                engine.camera.setFollowDistance(next);
                this.#followDistance = next;
            }
        }
    }

    class CameraService {
        #defaultScript = null;

        constructor() {
            Object.defineProperty(this, "ViewController", {
                value: CameraViewController, enumerable: true
            });
            Object.freeze(this);
        }

        createViewController(options = {}) {
            return new CameraViewController(options);
        }

        installDefaultScript(ScriptType) {
            if (this.#defaultScript !== null)
                throw new Error("Default player camera script is already installed");
            if (typeof ScriptType !== "function")
                throw new TypeError("Default camera script must be a class");
            const instance = new ScriptType();
            instance.install();
            this.#defaultScript = instance;
            return instance;
        }

        get defaultScript() { return this.#defaultScript; }
    }

    // A module registers exactly one class. This service is allocated once
    // per V8 context; instances retain their own private runtime state.
    class CharacterService {
        #Controller = null;

        constructor() { Object.freeze(this); }

        registerController(Controller) {
            if (this.#Controller !== null)
                throw new Error("NorthStar character controller is already registered");
            if (typeof Controller !== "function")
                throw new TypeError("Character controller must be a class");
            this.#Controller = Controller;
        }

        get Controller() { return this.#Controller; }

        createController(options = {}) {
            if (this.#Controller === null)
                throw new Error("Character.js has not registered its controller class");
            return new this.#Controller(options);
        }
    }

    // Project-owned player definitions are registered exactly once by
    // scripts/playerConfig.js, before all root lifecycle scripts. No named
    // character, model or input mapping is compiled into the engine.
    class PlayerService {
        #config = null;
        #index = -1;
        #paused = false;

        constructor() { Object.freeze(this); }

        configure(definition) {
            if (this.#config !== null)
                throw new Error("Player config already registered");
            if (!definition || typeof definition !== "object" ||
                typeof definition.enabled !== "boolean")
                throw new TypeError("playerConfig.js must specify enabled:boolean");
            const freezeTree = value => {
                if (!value || typeof value !== "object") return value;
                if (Array.isArray(value)) return Object.freeze(value.map(freezeTree));
                const result = {};
                for (const [key, item] of Object.entries(value))
                    result[key] = freezeTree(item);
                return Object.freeze(result);
            };
            this.#config = freezeTree(definition);
            return this.#config;
        }

        get config() {
            if (!this.#config) throw new Error("playerConfig.js was not evaluated");
            return this.#config;
        }
        get enabled() { return this.#config !== null && this.#config.enabled; }
        get index() { return this.#index; }
        setIndex(index) {
            if (!Number.isSafeInteger(index) || index < -1)
                throw new RangeError("Invalid player scene index");
            this.#index = index;
        }
        get paused() { return this.#paused; }
        setPaused(value) {
            if (typeof value !== "boolean") throw new TypeError("paused must be boolean");
            this.#paused = value;
        }
        reset() {
            this.#index = -1;
            this.#paused = false;
        }
    }

    // Public class exports consumed by project imports from @northstar/engine.
    // Calls remain provider-neutral and pass through replaceable engine.* APIs.
    class Camera {
        static available() { return engine.camera.available(); }
        static mode() { return engine.camera.mode(); }
        static setMode(mode, seconds = 0.45) {
            return engine.camera.setMode(mode, seconds);
        }
        static angles() { return engine.camera.angles(); }
        static setAngles(yaw, pitch) { return engine.camera.setAngles(yaw, pitch); }
        static followDistance() { return engine.camera.followDistance(); }
        static setFollowDistance(distance) {
            return engine.camera.setFollowDistance(distance);
        }
        static setZoom(amount) { return engine.camera.setZoom(amount); }
        static shake(amplitudeMetres, seconds) {
            return engine.camera.shake(amplitudeMetres, seconds);
        }
        static cut() { return engine.camera.cut(); }
        /** Highest priority request wins; ties go to the latest source. */
        static pushShot(source, pose, priority = 0, blendSeconds = 0.35) {
            if (typeof source !== "string" ||
                !/^[a-zA-Z0-9_.:-]{1,80}$/.test(source))
                throw new TypeError("Camera shot source must be a valid identifier");
            if (!pose || typeof pose !== "object" || !pose.position)
                throw new TypeError("Camera shot requires a world-space pose");
            const p = pose.position;
            const values = [
                p.x, p.y, p.z, pose.yaw, pose.pitch,
                pose.roll === undefined ? 0 : pose.roll,
                pose.fovY === undefined ? 75 : pose.fovY,
                priority, blendSeconds
            ];
            if (!values.every(Number.isFinite) ||
                !Number.isInteger(priority) || priority < -100 ||
                priority > 100 || blendSeconds < 0 || blendSeconds > 10)
                throw new RangeError("Invalid camera shot transform, priority or blend");
            return engine.camera.pushShot(source, ...values);
        }
        static clearShot(source) {
            if (typeof source !== "string" ||
                !/^[a-zA-Z0-9_.:-]{1,80}$/.test(source))
                throw new TypeError("Invalid camera shot source");
            return engine.camera.clearShot(source);
        }
        static clearShots() { return engine.camera.clearShots(); }
        static activeShot() { return engine.camera.activeShot(); }
        /** Read a named preset from project VFS gameplay/camera_presets.json. */
        static applyPreset(id) {
            if (typeof id !== "string" ||
                !/^[a-zA-Z0-9_.:-]{1,80}$/.test(id))
                throw new TypeError("Invalid camera preset identifier");
            return engine.camera.applyPreset(id);
        }
        static createViewController(options = {}) {
            return new CameraViewController(options);
        }
    }

    // Key enumerations are self-contained classes loaded from scripts/input/.
    // Register once per V8 context before other SDK/gameplay scripts execute.
    class InputTypes {
        #keys = null;
        #gamepadButtons = null;

        registerKeys(KeysType) {
            if (this.#keys !== null)
                throw new Error("Keyboard Keys class was already registered");
            if (typeof KeysType !== "function" || !Object.isFrozen(KeysType))
                throw new TypeError("Keys must be a frozen class");
            this.#keys = KeysType;
        }

        registerGamepadButtons(ButtonsType) {
            if (this.#gamepadButtons !== null)
                throw new Error("GamepadButtons class was already registered");
            if (typeof ButtonsType !== "function" || !Object.isFrozen(ButtonsType))
                throw new TypeError("GamepadButtons must be a frozen class");
            this.#gamepadButtons = ButtonsType;
        }

        get Keys() {
            if (this.#keys === null)
                throw new Error("scripts/input/Keys.js must load before gameplay");
            return this.#keys;
        }

        get GamepadButtons() {
            if (this.#gamepadButtons === null)
                throw new Error("scripts/input/GamepadButtons.js must load before gameplay");
            return this.#gamepadButtons;
        }
    }

    class SceneTypes {
        #entity = null;
        #prefab = null;
        registerPrefab(PrefabType) {
            if (this.#prefab !== null)
                throw new Error("Prefab class was already registered");
            if (typeof PrefabType !== "function")
                throw new TypeError("Prefab must be a class");
            this.#prefab = PrefabType;
        }
        get Prefab() {
            if (!this.#prefab)
                throw new Error("scripts/scene/Prefab.js must load before gameplay");
            return this.#prefab;
        }
        registerEntity(EntityType) {
            if (this.#entity !== null) throw new Error("Entity class was already registered");
            if (typeof EntityType !== "function")
                throw new TypeError("Scene Entity must be a class");
            this.#entity = EntityType;
        }
        get Entity() {
            if (!this.#entity)
                throw new Error("scripts/scene/Entity.js must load before gameplay");
            return this.#entity;
        }
    }

    class Input {
        static get Keys() { return NorthStar.inputTypes.Keys; }
        static get GamepadButtons() { return NorthStar.inputTypes.GamepadButtons; }
        static isDown(key) { return engine.input.isDown(key); }
        static mouseDown(button) { return engine.input.mouseDown(button); }
        static pointerDelta() { return engine.input.pointerDelta(); }
        static wheelDelta() { return engine.input.wheelDelta(); }
        static gamepad(index) { return engine.input.gamepad(index); }

        // The first connected pad, or null. Call once per frame and reuse.
        static firstGamepad(maxPlayers = 4) {
            if (!Number.isInteger(maxPlayers) || maxPlayers < 1 || maxPlayers > 16)
                throw new RangeError("maxPlayers must be an integer from 1 to 16");
            for (let index = 0; index < maxPlayers; ++index) {
                const pad = engine.input.gamepad(index);
                if (pad) return pad;
            }
            return null;
        }

        static hasButton(gamepad, button) {
            return Boolean(gamepad && ((gamepad.buttons & button) !== 0));
        }
    }

    class Events {
        static on(name, handler) { return engine.on(name, handler); }
        static off(name, handler) { return engine.off(name, handler); }
        static emit(name, payload) { return engine.emit(name, payload); }
        static once(name, handler) {
            if (typeof handler !== "function")
                throw new TypeError("Event listener must be a function");
            const unsubscribe = engine.on(name, value => {
                unsubscribe();
                handler(value);
            });
            return unsubscribe;
        }
    }

    class Character {
        static createController(options = {}) {
            return NorthStar.character.createController(options);
        }
        static get Controller() { return NorthStar.character.Controller; }
    }

    // Clear, minimal facades over the replaceable host capability namespaces.
    class Scene {
        // Page size matches the native engine.scene.objects maximum (512).
        static objects(start = 0, count = 256) {
            if (!Number.isSafeInteger(start) || start < 0 ||
                !Number.isSafeInteger(count) || count < 1 || count > 512)
                throw new RangeError("Scene page requires start >= 0 and count 1..512");
            return engine.scene.objects(start, count);
        }

        static objectCount() { return engine.scene.objectCount(); }

        static spawn(options = {}) {
            if (!options || typeof options !== "object" || Array.isArray(options))
                throw new TypeError("Scene.spawn requires an entity descriptor");
            const allowed = new Set([
                "name", "position", "size", "mass", "color", "asset", "layer", "parent"
            ]);
            for (const name of Object.keys(options)) {
                if (!allowed.has(name))
                    throw new RangeError("Unsupported Scene.spawn option: " + name);
            }
            const {name = "Runtime Entity", position, size = 1,
                mass = 0, color = 0x46b9d8, asset = "", layer = "Default",
                parent = null} = options;
            if (typeof name !== "string" || !name ||
                typeof layer !== "string" || !layer ||
                typeof asset !== "string" ||
                !position || ![position.x, position.y, position.z].every(Number.isFinite) ||
                !Number.isFinite(size) || size <= 0 || size > 1000 ||
                !Number.isFinite(mass) || mass < 0 || mass > 1000000 ||
                !Number.isInteger(color) || color < 0 || color > 0xffffff)
                throw new TypeError("Invalid Scene.spawn entity descriptor");
            let parentId = "0";
            if (parent !== null) {
                if (parent instanceof NorthStar.sceneTypes.Entity) {
                    if (!parent.alive)
                        throw new Error("Cannot parent to a destroyed entity");
                    parentId = parent.id;
                } else if (typeof parent === "string" && /^[0-9]+$/.test(parent)) {
                    parentId = parent;
                } else {
                    throw new TypeError("Scene.spawn parent must be an Entity or stable ID");
                }
            }
            const record = engine.scene.spawn(name, position.x, position.y,
                position.z, size, mass, color, asset, layer, parentId);
            return new NorthStar.sceneTypes.Entity(record);
        }

        static get(index) {
            if (!Number.isSafeInteger(index) || index < 0)
                throw new RangeError("Scene object index must be nonnegative");
            return Scene.objects(index, 1)[0] || null;
        }

        static *iterate(pageSize = 256) {
            if (!Number.isSafeInteger(pageSize) || pageSize < 1 || pageSize > 512)
                throw new RangeError("Scene page size must be 1..512");
            for (let start = 0; ; start += pageSize) {
                const page = Scene.objects(start, pageSize);
                for (const object of page) if (object) yield object;
                if (page.length < pageSize) return;
            }
        }

        static findByName(name) {
            if (typeof name !== "string" || !name)
                throw new TypeError("Scene object name must be a nonempty string");
            for (const object of Scene.iterate()) {
                if (object.name === name) return object;
            }
            return null;
        }

        // Ordered by scene index, not proximity. maxResults is an explicit
        // query budget to avoid unbounded allocations on large maps.
        static nearby(center, radius, maxResults = 256) {
            if (!center || ![center.x, center.y, center.z].every(Number.isFinite) ||
                !Number.isFinite(radius) || radius < 0 ||
                !Number.isSafeInteger(maxResults) || maxResults < 1 ||
                maxResults > 16384)
                throw new RangeError("Invalid scene sphere query");
            const result = [];
            const squared = radius * radius;
            for (const object of Scene.iterate()) {
                const p = object.position;
                const dx = p.x - center.x;
                const dy = p.y - center.y;
                const dz = p.z - center.z;
                if (dx * dx + dy * dy + dz * dz <= squared) {
                    result.push(object);
                    if (result.length === maxResults) break;
                }
            }
            return result;
        }

        static findPlayer() {
            for (const object of Scene.iterate()) {
                if (object.player) return object;
            }
            return null;
        }

        static moveStatic(index, position) {
            if (!position || ![position.x, position.y, position.z].every(Number.isFinite))
                throw new TypeError("Scene position requires finite x/y/z");
            if (Math.max(Math.abs(position.x), Math.abs(position.y),
                         Math.abs(position.z)) > 1e6)
                throw new RangeError("Scene position is outside world bounds");
            return engine.scene.moveStatic(index,
                position.x, position.y, position.z);
        }

        static setColor(index, rgb) {
            if (!Number.isInteger(rgb) || rgb < 0 || rgb > 0xffffff)
                throw new RangeError("Scene RGB color must be 0x000000..0xFFFFFF");
            return engine.scene.setColor(index, rgb);
        }

        // Per-entity native 8-weight YDD/YMT playback. The C++ host owns
        // VFS validation, clip selection and renderer synchronization.
        static playAnimation(index, clipReference) {
            if (!Number.isSafeInteger(index) || index < 0 ||
                typeof clipReference !== "string" ||
                !/^[^\\:@]+\.ycd@[^\\/:@]+$/.test(clipReference) ||
                clipReference.includes("..") || clipReference.length > 512)
                throw new TypeError("Expected native YCD dictionary@clip reference");
            return engine.scene.setAnimation(index, clipReference);
        }

        static stopAnimation(index) {
            if (!Number.isSafeInteger(index) || index < 0)
                throw new RangeError("Scene animation index must be nonnegative");
            return engine.scene.setAnimation(index, "");
        }
    }

    class Physics {
        static sweepSphere(from, to, radius, ignoreIndex = -1) {
            return engine.physics.sweepSphere(
                from.x, from.y, from.z, to.x, to.y, to.z,
                radius, ignoreIndex);
        }

        static applyImpulse(index, vector) {
            return engine.physics.applyImpulse(index,
                vector.x, vector.y, vector.z);
        }
    }

    class Session {
        static pause(paused) { return engine.session.pause(paused); }
        static quit() { return engine.session.quit(); }
        static restart() { return engine.session.restart(); }
    }

    class UI {
        static setTitle(text) { return engine.ui.setTitle(text); }
    }

    class Logger {
        static debug(message) { return engine.logger.debug(message); }
        static info(message) { return engine.logger.info(message); }
        static warn(message) { return engine.logger.warn(message); }
        static error(message) { return engine.logger.error(message); }
    }

    class Player {
        static configure(definition) { return NorthStar.player.configure(definition); }
        static get config() { return NorthStar.player.config; }
        static get enabled() { return NorthStar.player.enabled; }
        static get index() { return NorthStar.player.index; }
        static setIndex(index) { return NorthStar.player.setIndex(index); }
        static get paused() { return NorthStar.player.paused; }
        static setPaused(value) { return NorthStar.player.setPaused(value); }
        static move(x, z) { return engine.player.move(x, z); }
        static jump() { return engine.player.jump(); }
        static eye() { return engine.player.eye(); }
        static setEyeHeight(height) { return engine.player.setEyeHeight(height); }
    }

    // Base lifecycle for independent root-script classes. A root class
    // registers once and keeps its mutable state in private instance fields.
    class Script {
        #installed = false;

        install() {
            if (this.#installed)
                throw new Error("Root script already installed");
            this.#installed = true;
            Events.on("start", () => this.start());
            Events.on("update", dt => this.update(dt));
            Events.on("stop", () => this.stop());
            return this;
        }

        start() {}
        update(_dt) {}
        stop() {}
    }

    class NorthStarSDK {
        constructor() {
            const inputTypes = new InputTypes();
            const engineModules = {
                Camera, Input, Events, Character, Player, Script,
                Scene, Physics, Session, UI, Logger
            };
            // The class references are resolved when an author imports them,
            // after the two input modules have registered. Avoid placeholders.
            Object.defineProperties(engineModules, {
                Keys: {enumerable: true, get: () => inputTypes.Keys},
                GamepadButtons: {
                    enumerable: true, get: () => inputTypes.GamepadButtons
                },
                Entity: {
                    enumerable: true, get: () => globalThis.NorthStar.sceneTypes.Entity
                },
                Prefab: {
                    enumerable: true, get: () => globalThis.NorthStar.sceneTypes.Prefab
                }
            });
            Object.freeze(engineModules);
            Object.defineProperties(this, {
                apiVersion: {value: 1, enumerable: true},
                camera: {value: new CameraService(), enumerable: true},
                character: {value: new CharacterService(), enumerable: true},
                player: {value: new PlayerService(), enumerable: true},
                inputTypes: {value: inputTypes, enumerable: false},
                sceneTypes: {value: new SceneTypes(), enumerable: false},
                modules: {
                    value: Object.freeze({engine: engineModules}),
                    enumerable: true
                }
            });
            Object.freeze(this);
        }
    }

    Object.defineProperty(globalThis, "NorthStar", {
        value: new NorthStarSDK(),
        writable: false, configurable: false, enumerable: false
    });
})();
