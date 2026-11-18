// NorthStarCpp shared scripting SDK: class-based, per-context services.
// Loads before scripts/character/Character.js, scripts/camera/playerCamera.js and project scripts.
// Only thin engine API contracts are required; gameplay remains project-owned.
(() => {
    "use strict";

    if (Object.prototype.hasOwnProperty.call(globalThis, "NorthStar"))
        throw new Error("NorthStar shared script API already registered");

    // One engine-owned validation contract. All high-level service facades
    // delegate to host providers; no secondary physics/scene implementation.
    class Contracts {
        static options(value, label = "Options") {
            if (value === null || typeof value !== "object" ||
                Array.isArray(value))
                throw new TypeError(label + " must be an object");
            return value;
        }
        static number(value, label, min = -Infinity, max = Infinity) {
            if (typeof value !== "number" || !Number.isFinite(value) ||
                value < min || value > max)
                throw new RangeError(label + " must be a finite number within " +
                    min + ".." + max);
            return value;
        }
        static identifier(value, label = "Identifier") {
            if (typeof value !== "string" ||
                !/^[a-zA-Z0-9_.:-]{1,80}$/.test(value))
                throw new TypeError(label + " must be a valid identifier");
            return value;
        }
        static index(value, label = "Scene index") {
            if (!Number.isSafeInteger(value) || value < 0)
                throw new RangeError(label + " must be a nonnegative integer");
            return value;
        }
        static vector(value, label = "Vector3", maxAbs = 1e6) {
            if (!value || typeof value !== "object" || Array.isArray(value))
                throw new TypeError(label + " must be {x,y,z}");
            for (const axis of ["x","y","z"])
                Contracts.number(value[axis], label + "." + axis, -maxAbs, maxAbs);
            return value;
        }
        static vfs(path, extension, prefix = "") {
            if (typeof path !== "string" || path.length > 512 ||
                !/^[a-zA-Z0-9_./-]+$/.test(path) ||
                path.startsWith("/") || path.includes("..") || path.includes("//") ||
                (prefix && !path.startsWith(prefix)) ||
                (extension && !path.endsWith(extension)))
                throw new TypeError("Invalid project-relative VFS asset path: " + path);
            return path;
        }
    }

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
            const current = Camera.followDistance();
            if (!Number.isFinite(current))
                throw new RangeError("Camera follow distance is not finite");
            this.#followDistance = this.#clamp(current);
            if (this.#followDistance !== current)
                Camera.setFollowDistance(this.#followDistance);
        }

        start() {
            this.#initializeDistance();
            // A key held during startup must not immediately reverse the view.
            this.#previousKeyDown = Input.isDown(this.#toggleKey);
            Camera.setMode(this.#initialMode, 0);
            this.#started = true;
        }

        stop() {
            this.#started = false;
            this.#previousKeyDown = false;
        }

        mode() { return Camera.mode(); }

        distance() {
            if (this.#followDistance === null) this.#initializeDistance();
            return this.#followDistance;
        }

        setMode(mode, duration = this.#transitionSeconds) {
            if (!CameraViewController.MODES.includes(mode))
                throw new RangeError("Unknown camera view");
            this.#validateDuration(duration);
            Camera.setMode(mode, duration);
            return mode;
        }

        toggle() {
            return this.setMode(this.mode() === "first_person"
                ? "third_person" : "first_person");
        }

        update() {
            if (!this.#started) return;
            const down = Input.isDown(this.#toggleKey);
            if (down && !this.#previousKeyDown) this.toggle();
            this.#previousKeyDown = down;

            // Wheel units are signed, fractional notches. Positive is closer.
            const steps = Input.wheelDelta();
            if (!Number.isFinite(steps))
                throw new RangeError("Non-finite camera wheel input");
            if (this.mode() !== "third_person" || steps === 0) return;
            if (this.#followDistance === null) this.#initializeDistance();
            const next = this.#clamp(this.#followDistance - steps * this.#wheelStep);
            if (next !== this.#followDistance) {
                Camera.setFollowDistance(next);
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
        static available() { return engine.camera.views.available(); }
        static mode() { return engine.camera.views.mode(); }
        static setMode(mode, seconds = 0.45) {
            return engine.camera.views.setMode(mode, seconds);
        }
        static angles() { return engine.camera.views.angles(); }
        static setAngles(yaw, pitch) { return engine.camera.views.setAngles(yaw, pitch); }
        static followDistance() { return engine.camera.views.followDistance(); }
        static setFollowDistance(distance) {
            return engine.camera.views.setFollowDistance(distance);
        }
        static setZoom(amount) { return engine.camera.views.setZoom(amount); }
        static shake(amplitudeMetres, seconds) {
            return engine.camera.effects.shake(amplitudeMetres, seconds);
        }
        static recoil(pitchRadians,yawRadians=0,seconds=.24) {
            return engine.camera.effects.recoil(pitchRadians,yawRadians,seconds);
        }
        static cut() { return engine.camera.shots.cut(); }
        /** Highest priority request wins; ties go to the latest source. */
        static pushShot(source, pose, priority = 0, blendSeconds = 0.35) {
            Contracts.identifier(source, "Camera shot source");
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
            return engine.camera.shots.pushShot(source, ...values);
        }
        static clearShot(source) {
            Contracts.identifier(source, "Camera shot source");
            return engine.camera.shots.clearShot(source);
        }
        static clearShots() { return engine.camera.shots.clearShots(); }
        static activeShot() { return engine.camera.shots.activeShot(); }
        /** Read a named preset from project VFS gameplay/camera_presets.json. */
        static applyPreset(id) {
            Contracts.identifier(id, "Camera preset identifier");
            return engine.camera.shots.applyPreset(id);
        }
        /** Drive a camera by world-space point-of-interest, without project math. */
        static lookAt(source, position, target, {fovY = 75,
                           priority = 0, blendSeconds = 0.35} = {}) {
            Contracts.vector(position,"Camera position");
            Contracts.vector(target,"Camera target");
            const dx = target.x-position.x, dy = target.y-position.y,
                  dz = target.z-position.z;
            if (Math.hypot(dx,dy,dz) < 1e-6)
                throw new RangeError("Camera look-at target coincides with eye");
            if (!Number.isFinite(fovY) || fovY <= 1 || fovY >= 179)
                throw new RangeError("Camera field of view must be 1..179 degrees");
            return Camera.pushShot(source, {
                position, yaw: Math.atan2(dx,dz),
                pitch: Math.atan2(dy,Math.hypot(dx,dz)), fovY
            }, priority, blendSeconds);
        }
        static #asset(path) {
            return Contracts.vfs(path,"");
        }
        static playTrack(path, loop = false, blendSeconds = 0.5) {
            Camera.#asset(path);
            if (typeof loop !== "boolean" || !Number.isFinite(blendSeconds) ||
                blendSeconds < 0 || blendSeconds > 10)
                throw new RangeError("Invalid camera track options");
            return engine.camera.cinematics.playTrack(path,loop,blendSeconds);
        }
        static playCutscene(path, {loop = false,
                entrySeconds = 0.55,exitSeconds = 0.55} = {}) {
            Camera.#asset(path);
            if (typeof loop !== "boolean" ||
                ![entrySeconds,exitSeconds].every(n =>
                    Number.isFinite(n) && n >= 0 && n <= 10))
                throw new RangeError("Invalid camera cutscene transition");
            return engine.camera.cinematics.playCutscene(path,loop,entrySeconds,exitSeconds);
        }
        static stopCutscene(exitSeconds = 0.55) {
            if (!Number.isFinite(exitSeconds) || exitSeconds < 0 || exitSeconds > 10)
                throw new RangeError("Invalid cutscene exit time");
            return engine.camera.cinematics.stopCutscene(exitSeconds);
        }
        static pauseCutscene(paused = true) {
            if (typeof paused !== "boolean") throw new TypeError("Expected pause boolean");
            return engine.camera.cinematics.pauseCutscene(paused);
        }
        static seekCutscene(seconds) {
            if (!Number.isFinite(seconds) || seconds < 0)
                throw new RangeError("Cutscene seek time must be nonnegative");
            return engine.camera.cinematics.seekCutscene(seconds);
        }
        static cutsceneState() { return engine.camera.cinematics.cutsceneState(); }
        static lensState() { return engine.camera.cinematics.lensState(); }

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
        static isDown(key) { return engine.input.keyboard.isDown(key); }
        static mouseDown(button) { return engine.input.pointer.mouseDown(button); }
        static pointerDelta() { return engine.input.pointer.pointerDelta(); }
        static wheelDelta() { return engine.input.pointer.wheelDelta(); }
        static gamepad(index) { return engine.input.gamepads.gamepad(index); }

        // The first connected pad, or null. Call once per frame and reuse.
        static firstGamepad(maxPlayers = 4) {
            if (!Number.isInteger(maxPlayers) || maxPlayers < 1 || maxPlayers > 16)
                throw new RangeError("maxPlayers must be an integer from 1 to 16");
            for (let index = 0; index < maxPlayers; ++index) {
                const pad = engine.input.gamepads.gamepad(index);
                if (pad) return pad;
            }
            return null;
        }

        static hasButton(gamepad, button) {
            return Boolean(gamepad && ((gamepad.buttons & button) !== 0));
        }
        static action(key, button = null, gamepad = null) {
            return Input.isDown(key) ||
                (button !== null && Input.hasButton(gamepad, button));
        }
        static axis(positive, negative, analogue = 0,
                    gamepadPositive = null, gamepadNegative = null, gamepad = null) {
            return Number(Input.isDown(positive)) - Number(Input.isDown(negative)) +
                (Number.isFinite(analogue) ? analogue : 0) +
                Number(gamepadPositive !== null && Input.hasButton(gamepad, gamepadPositive)) -
                Number(gamepadNegative !== null && Input.hasButton(gamepad, gamepadNegative));
        }
        static createAction(key, button = null) {
            return new InputAction(key, button);
        }

        /** Clamp diagonal input to length 1, preserving analogue magnitude. */
        static normalized2D(x, y) {
            if (![x,y].every(Number.isFinite))
                throw new TypeError("Input.normalized2D expects finite x and y");
            const length = Math.hypot(x,y);
            return length > 1 ? {x:x/length,y:y/length} : {x,y};
        }
    }

    class InputAction {
        #key;
        #button;
        #wasDown = false;
        constructor(key, button = null) {
            if (!Number.isInteger(key) || key < 0 || key > 255 ||
                (button !== null && (!Number.isInteger(button) || button < 0)))
                throw new RangeError("Invalid input action key or button");
            this.#key = key;
            this.#button = button;
        }
        held(gamepad = null) { return Input.action(this.#key, this.#button, gamepad); }
        pressed(gamepad = null) {
            const down = this.held(gamepad);
            const result = down && !this.#wasDown;
            this.#wasDown = down;
            return result;
        }
        reset() { this.#wasDown = false; }
    }

    // Per-owner event subscription lifetime, shared by all root scripts.
    // The native engine.on/off dispatcher remains the only event bus.
    class EventScope {
        #subscriptions = new Set();
        #disposed = false;

        on(name, handler) {
            if (this.#disposed)
                throw new Error("Event scope has already been disposed");
            const unsubscribe = Events.on(name, handler);
            this.#subscriptions.add(unsubscribe);
            let active = true;
            return () => {
                if (!active) return false;
                active = false;
                this.#subscriptions.delete(unsubscribe);
                return unsubscribe();
            };
        }

        dispose() {
            if (this.#disposed) return;
            this.#disposed = true;
            for (const unsubscribe of this.#subscriptions)
                unsubscribe();
            this.#subscriptions.clear();
        }
    }

    class Events {
        static #contracts = new Map();
        static #subscriptions = new Map();
        static #emitted = new Map();
        static #recent = [];
        static #sequence = 0;
        static #validateName(name) {
            if (typeof name !== "string" ||
                !/^[a-zA-Z_][a-zA-Z0-9_.:-]{0,95}$/.test(name))
                throw new TypeError("Invalid event identifier");
        }

        static define(name, validator = null) {
            Events.#validateName(name);
            if (validator !== null && typeof validator !== "function")
                throw new TypeError("Validator must be a function");
            if (Events.#contracts.size >= 256)
                throw new RangeError("Too many event contracts");
            if (Events.#contracts.has(name))
                throw new Error("Event already declared: " + name);
            Events.#contracts.set(name, validator);
        }

        static on(name, handler) {
            Events.#validateName(name);
            if (typeof handler !== "function")
                throw new TypeError("Event listener must be a function");
            const nativeDispose = engine.on(name, handler);
            let records = Events.#subscriptions.get(name);
            if (!records) {
                records = new Map();
                Events.#subscriptions.set(name, records);
            }
            let active = true;
            const dispose = () => {
                if (!active) return false;
                active = false;
                if (typeof nativeDispose === "function") nativeDispose();
                else if (typeof engine.off === "function") engine.off(name, handler);
                records.delete(dispose);
                if (!records.size) Events.#subscriptions.delete(name);
                return true;
            };
            records.set(dispose, handler);
            return dispose;
        }
        static off(name, handler) {
            Events.#validateName(name);
            const records = Events.#subscriptions.get(name);
            if (!records) return false;
            let removed = false;
            for (const [dispose, callback] of [...records]) {
                if (callback === handler) removed = dispose() || removed;
            }
            return removed;
        }
        static emit(name, payload = null) {
            Events.#validateName(name);
            const validate = Events.#contracts.get(name);
            if (validate && !validate(payload))
                throw new TypeError("Invalid event payload: " + name);
            const result = engine.emit(name, payload);
            // Only events published through Events.emit are recorded;
            // raw native "start"/"update" signals bypass this lightweight log.
            if (Events.#emitted.size < 256 || Events.#emitted.has(name))
                Events.#emitted.set(name, (Events.#emitted.get(name) || 0) + 1);
            Events.#recent.push(Object.freeze({sequence: ++Events.#sequence, name}));
            if (Events.#recent.length > 64) Events.#recent.shift();
            return result;
        }

        static scope() { return new EventScope(); }

        static stats() {
            const subscriptions = {};
            for (const [name, records] of Events.#subscriptions)
                subscriptions[name] = records.size;
            return Object.freeze({
                declared: Events.#contracts.size,
                emitted: Events.#sequence,
                emittedByEvent: Object.freeze(
                    Object.fromEntries(Events.#emitted)),
                subscribers: Object.values(subscriptions).reduce(
                    (sum, count) => sum + count, 0),
                byEvent: Object.freeze(subscriptions)
            });
        }
        static history(limit = 32) {
            if (!Number.isSafeInteger(limit) || limit < 1 || limit > 64)
                throw new RangeError("Event history limit must be 1..64");
            return Object.freeze(Events.#recent.slice(-limit));
        }

        static once(name, handler) {
            if (typeof handler !== "function")
                throw new TypeError("Event listener must be a function");
            let unsubscribe;
            unsubscribe = Events.on(name, value => {
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

    // Non-owning authored-entity handle. Keep stable identity, not just
    // an array slot; an index can be reused by a different ECS entity.
    class AuthoredEntityHandle {
        #id;
        #index;
        constructor(record) {
            if (!record || typeof record.id !== "string" ||
                !Number.isSafeInteger(record.index) || record.index < 0)
                throw new TypeError("Invalid authored entity record");
            this.#id = record.id;
            this.#index = record.index;
            Object.freeze(this);
        }
        get id() { return this.#id; }
        get index() { return this.#index; }
        get alive() {
            return Scene.get(this.#index)?.id === this.#id;
        }
        get snapshot() {
            const record = Scene.get(this.#index);
            return record?.id === this.#id ? record : null;
        }
        get position() { return this.snapshot?.position ?? null; }
        getComponent(name) {
            this.#requireAlive();
            return ECS.get(this,name);
        }
        hasComponent(name) {
            return this.alive && ECS.has(this,name);
        }
        setComponent(name,value) {
            this.#requireAlive();
            ECS.set(this,name,value);
            return this;
        }

        #requireAlive() {
            if (!this.alive)
                throw new Error("Authored entity handle is stale: " + this.#id);
        }
        setPosition(value) {
            this.#requireAlive();
            Scene.moveStatic(this.#index,value);
            return this;
        }
        setRotation(value) {
            this.#requireAlive();
            Scene.rotateStatic(this.#index,value);
            return this;
        }
    }

    // Clear, minimal facades over the replaceable host capability namespaces.
    class Scene {
        // Allocated once per V8 isolate, not on every entity spawn.
        static #spawnOptionNames = new Set([
            "name", "position", "size", "mass", "color", "asset", "layer",
            "parent", "collision", "scale"
        ]);
        // Page size matches the native engine.scene.entities.objects maximum (512).
        static objects(start = 0, count = 256) {
            if (!Number.isSafeInteger(start) || start < 0 ||
                !Number.isSafeInteger(count) || count < 1 || count > 512)
                throw new RangeError("Scene page requires start >= 0 and count 1..512");
            return engine.scene.entities.objects(start, count);
        }

        static objectCount() { return engine.scene.entities.objectCount(); }

        static spawn(options = {}) {
            if (!options || typeof options !== "object" || Array.isArray(options))
                throw new TypeError("Scene.spawn requires an entity descriptor");
            for (const name of Object.keys(options)) {
                if (!Scene.#spawnOptionNames.has(name))
                    throw new RangeError("Unsupported Scene.spawn option: " + name);
            }
            const {name = "Runtime Entity", position, size = 1,
                mass = 0, color = 0x46b9d8, asset = "", layer = "Default",
                parent = null, collision = true,
                scale = {x:1,y:1,z:1}} = options;
            if (typeof name !== "string" || !name ||
                typeof layer !== "string" || !layer ||
                typeof asset !== "string" ||
                !position || ![position.x, position.y, position.z].every(Number.isFinite) ||
                !Number.isFinite(size) || size <= 0 || size > 1000 ||
                !Number.isFinite(mass) || mass < 0 || mass > 1000000 ||
                typeof collision !== "boolean" ||
                !scale || ![scale.x,scale.y,scale.z].every(
                    value=>Number.isFinite(value) && value>0 && value<=100) ||
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
            const args = [name, position.x, position.y, position.z, size, mass,
                color, asset, layer, parentId];
            if(scale.x!==1 || scale.y!==1 || scale.z!==1) {
                // Visual mesh scale independent of the physics proxy size.
                args.push(collision,scale.x,scale.y,scale.z);
            } else if(!collision)args.push(false);
            const record = engine.scene.entities.spawn(...args);
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

        /** Use for required authored scene dependencies with useful errors. */
        static requireByName(name) {
            const object = Scene.findByName(name);
            if (!object) throw new Error('Scene entity "' + name + '" was not found');
            return object;
        }

        /** Handle to an already-authored entity, without instancing a prefab. */
        static handleByName(name) {
            const record = Scene.findByName(name);
            return record ? new AuthoredEntityHandle(record) : null;
        }
        static handle(index) {
            const record = Scene.get(Contracts.index(index,"Entity index"));
            return record ? new AuthoredEntityHandle(record) : null;
        }

        /** Strong alternative to Scene.get() for required entity indices. */
        static require(index) {
            const object = Scene.get(index);
            if (!object) throw new Error("Scene entity index " + index + " does not exist");
            return object;
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

        static findPawn() {
            const index = engine.player.controller.controlledIndex();
            return index < 0 ? null : Scene.get(index);
        }
        // Deprecated name retained as a thin alias; no duplicate O(n) scan.
        static findPlayer() { return Scene.findPawn(); }

        static moveStatic(index, position) {
            if (!position || ![position.x, position.y, position.z].every(Number.isFinite))
                throw new TypeError("Scene position requires finite x/y/z");
            if (Math.max(Math.abs(position.x), Math.abs(position.y),
                         Math.abs(position.z)) > 1e6)
                throw new RangeError("Scene position is outside world bounds");
            return engine.scene.transforms.moveStatic(index,
                position.x, position.y, position.z);
        }

        static rotateStatic(index, rotation) {
            if (!Number.isSafeInteger(index) || index < 0 ||
                !rotation || ![rotation.x, rotation.y, rotation.z].every(Number.isFinite))
                throw new TypeError("Scene rotation requires a valid entity index and finite Euler angles");
            if (Math.max(Math.abs(rotation.x), Math.abs(rotation.y),
                         Math.abs(rotation.z)) > 360000)
                throw new RangeError("Scene rotation exceeds the supported limits");
            return engine.scene.transforms.rotateStatic(index,
                rotation.x, rotation.y, rotation.z);
        }

        static setColor(index, rgb) {
            if (!Number.isInteger(rgb) || rgb < 0 || rgb > 0xffffff)
                throw new RangeError("Scene RGB color must be 0x000000..0xFFFFFF");
            return engine.scene.transforms.setColor(index, rgb);
        }

        /** Render presentation of an existing skinned Entity, not a prefab swap.
         *  The camera rig stays an independent scene object. */
        static setVisualMode(index, mode, settings = {}) {
            if (!Number.isSafeInteger(index) || index < 0 ||
                (mode !== "world" && mode !== "first_person_arms" &&
                 mode !== "first_person_body"))
                throw new TypeError("Invalid scene visual mode");
            if (settings == null || typeof settings !== "object")
                throw new TypeError("Invalid camera presentation settings");
            const forward = settings.forward ?? 0.55;
            const drop = settings.drop ?? 0.98;
            const pitch = settings.armPitch ?? 55;
            // Authored sight-line lateral offset (metres, +right). This is
            // also used by the attached character's two-hand grip solver.
            const right = settings.right ?? 0;
            // Explicitly tag aim-down-sights; no arbitrary drop-height heuristic.
            const ads = settings.ads === true;
            const recoilBack = settings.recoilBack ?? 0;
            const recoilArm = settings.recoilArm ?? 0;
            if (settings.ads !== undefined && typeof settings.ads !== "boolean")
                throw new TypeError("ADS presentation state must be boolean");
            if (![forward, drop, pitch, right,recoilBack,recoilArm].every(Number.isFinite) ||
                recoilBack < 0 || recoilBack > 0.06 ||
                recoilArm < 0 || recoilArm > 1 ||
                forward < 0 || forward > 3 || drop < 0 || drop > 3 ||
                Math.abs(pitch) > 180 || Math.abs(right) > 1)
                throw new RangeError("Invalid first-person presentation/grip offsets");
            return engine.scene.animation.setVisualMode(index, mode, forward, drop,
                pitch, right, ads, recoilBack, recoilArm);
        }

        // Per-entity native 8-weight YDD/YMT playback. The C++ host owns
        // VFS validation, clip selection and renderer synchronization.
        // Native scene-graph socket attachment; generic for any skinned YDD.
        // The attachment is evaluated with the same joint pose as rendering.
        static attachToBone(childIndex,parentIndex,bone,offset={},rotation={},alignment="socket") {
            if(!Number.isSafeInteger(childIndex) || childIndex<0 ||
               !Number.isSafeInteger(parentIndex) || parentIndex<0 ||
               typeof bone!=="string" || !/^[A-Za-z0-9_]{1,96}$/.test(bone))
                throw new TypeError("Invalid scene bone attachment");
            const pos=[offset.x??0,offset.y??0,offset.z??0];
            const angles=[rotation.x??0,rotation.y??0,rotation.z??0];
            if(![...pos,...angles].every(x=>typeof x==="number" &&
                Number.isFinite(x) && Math.abs(x)<=360))
                throw new RangeError("Invalid bone socket offset/angles");
            if(!["socket","camera_forward"].includes(alignment))
                throw new RangeError("Unknown bone alignment mode");
            return engine.scene.animation.attachBone(childIndex,parentIndex,bone,...pos,...angles,alignment);
        }

        // A weapon-local point transformed by the REAL skinned attachment.
        // A missing skeletal frame returns null rather than a camera guess.
        static attachmentPoint(childIndex, offset) {
            if(!Number.isSafeInteger(childIndex) || childIndex<0 ||
               !offset || ![offset.x,offset.y,offset.z].every(Number.isFinite))
                throw new TypeError("Scene.attachmentPoint needs entity index and local xyz");
            return engine.scene.animation.attachmentPoint(
                childIndex,offset.x,offset.y,offset.z);
        }

        static detachFromBone(childIndex) {
            if(!Number.isSafeInteger(childIndex)||childIndex<0)
                throw new RangeError("Invalid scene bone attachment index");
            return engine.scene.animation.detachBone(childIndex);
        }

        static playAnimation(index, clipReference, playbackRate = 1) {
            if (!Number.isFinite(playbackRate) || playbackRate < 0.1 || playbackRate > 4)
                throw new RangeError("Animation playback rate must be 0.1..4");
            if (!Number.isSafeInteger(index) || index < 0 ||
                typeof clipReference !== "string" ||
                !/^[^\\:@]+\.ycd@[^\\/:@]+$/.test(clipReference) ||
                clipReference.includes("..") || clipReference.length > 512)
                throw new TypeError("Expected native YCD dictionary@clip reference");
            return engine.scene.animation.setAnimation(index, clipReference, playbackRate);
        }

        static stopAnimation(index) {
            if (!Number.isSafeInteger(index) || index < 0)
                throw new RangeError("Scene animation index must be nonnegative");
            return engine.scene.animation.setAnimation(index, "");
        }
    }

    class Physics {
        // The native response provider exposes a DESTRUCTIVE contact drain.
        // Exactly one owner drains it per game update; consumers receive the
        // same immutable batch instead of competing for native messages.
        static #contactListeners = new Set();
        static #contactUpdate = null;
        static #contactStop = null;
        static #contactSnapshot = Object.freeze([]);
        static #contactEpoch = 0;

        static #dispatchContacts(dt) {
            if (typeof dt !== "number" || !Number.isFinite(dt) || dt < 0)
                return;
            const native = typeof engine.physics?.contacts?.pollContacts === "function" ?
                engine.physics.contacts.pollContacts(256) : [];
            if (!Array.isArray(native))
                throw new TypeError("Native physics contacts must be an array");
            const frozen = Object.freeze(native.map(contact => {
                if (!contact || typeof contact !== "object")
                    throw new TypeError("Malformed physics contact event");
                return Object.freeze({...contact});
            }));
            Physics.#contactSnapshot = frozen;
            Physics.#contactEpoch++;
            for (const listener of [...Physics.#contactListeners]) {
                if (!Physics.#contactListeners.has(listener)) continue;
                try { listener(frozen,dt); }
                catch(error) { Logger.warn("[engine.physics] contact subscriber: " +
                    String(error)); }
            }
        }

        static #detachContactDispatcher() {
            const update = Physics.#contactUpdate, stop = Physics.#contactStop;
            Physics.#contactUpdate = Physics.#contactStop = null;
            if (typeof update === "function") update();
            if (typeof stop === "function") stop();
        }

        static onContacts(listener) {
            if (typeof listener !== "function")
                throw new TypeError("Physics.onContacts requires a listener");
            if (typeof engine.on !== "function")
                throw new Error("Native engine event provider is unavailable");
            Physics.#contactListeners.add(listener);
            if (!Physics.#contactUpdate) {
                Physics.#contactUpdate = Events.on("update",
                    dt => Physics.#dispatchContacts(dt));
                Physics.#contactStop = Events.on("stop", () => Physics.resetContacts());
            }
            let active = true;
            return () => {
                if (!active) return false;
                active = false;
                Physics.#contactListeners.delete(listener);
                if (!Physics.#contactListeners.size)
                    Physics.#detachContactDispatcher();
                return true;
            };
        }

        static resetContacts() {
            Physics.#contactListeners.clear();
            Physics.#contactSnapshot = Object.freeze([]);
            Physics.#detachContactDispatcher();
        }

        static contacts() { return Physics.#contactSnapshot; }
        static get contactEpoch() { return Physics.#contactEpoch; }

        static raycast(from,to,ignoreIndex=-1) {
            Contracts.vector(from,"Raycast origin");
            Contracts.vector(to,"Raycast target");
            if(!Number.isSafeInteger(ignoreIndex)||ignoreIndex< -1)
                throw new RangeError("Invalid ignored scene index");
            Capabilities.require("physics.raycast");
            return engine.physics.queries.raycast(from.x,from.y,from.z,
                to.x,to.y,to.z,ignoreIndex);
        }
        static sweepSphereChannel(from,to,radius,channel,ignoreIndex=-1) {
            Contracts.vector(from,"Sweep origin");
            Contracts.vector(to,"Sweep target");
            Contracts.number(radius,"Sweep radius",Number.MIN_VALUE,100);
            Contracts.identifier(channel,"Collision channel");
            if(!Number.isSafeInteger(ignoreIndex)||ignoreIndex< -1)
                throw new RangeError("Invalid ignored scene index");
            if(typeof engine.physics?.queries?.sweepSphereChannel!=="function")
                throw new EngineCapabilityError("physics.sweepSphereChannel");
            return engine.physics.queries.sweepSphereChannel(
                from.x,from.y,from.z,to.x,to.y,to.z,radius,channel,ignoreIndex);
        }

        static sweepSphere(from, to, radius, ignoreIndex = -1) {
            Contracts.vector(from,"Sweep origin");
            Contracts.vector(to,"Sweep destination");
            if (!Number.isFinite(radius) || radius <= 0 || radius > 100 ||
                !Number.isSafeInteger(ignoreIndex) || ignoreIndex < -1)
                throw new RangeError("Invalid radius or ignored scene index");
            return engine.physics.queries.sweepSphere(
                from.x, from.y, from.z, to.x, to.y, to.z,
                radius, ignoreIndex);
        }
        // Native provider contact events; -1 represents the implicit
        // physics ground. Begin/End are delivered at most once per pair.
        static pollContacts(max=256) {
            if(!Number.isSafeInteger(max) || max<1 || max>256)
                throw new RangeError("Physics contact batch must be 1..256");
            if(typeof engine.physics?.contacts?.pollContacts!=="function")
                return [];
            // Compatibility: while a dispatcher is active, this is a
            // non-consuming view. Outside it, keep legacy polling available.
            return Physics.#contactUpdate ?
                Physics.#contactSnapshot.slice(0,max) :
                engine.physics.contacts.pollContacts(max);
        }
        static setBodyMaterial(index,{friction=.30,restitution=.42}={}) {
            if(!Number.isSafeInteger(index)||index<0 ||
               !Number.isFinite(friction)||friction<0||friction>1 ||
               !Number.isFinite(restitution)||restitution<0||restitution>1)
                throw new RangeError("Invalid physics body surface material");
            if(typeof engine.physics?.bodies?.setBodyMaterial!=="function")return false;
            return Boolean(engine.physics.bodies.setBodyMaterial(index,friction,restitution));
        }
        static applyImpulse(index, vector) {
            if (!Number.isSafeInteger(index) || index < 0)
                throw new RangeError("Physics index must be nonnegative");
            Contracts.vector(vector,"Impulse");
            return engine.physics.bodies.applyImpulse(index,
                vector.x, vector.y, vector.z);
        }
    }

    // Provider-agnostic FPS ballistics; weapon definitions only supply local
    // muzzle metadata. The renderer owns attachment transforms, while this
    // shared script owns sight-to-muzzle convergence and collision queries.
    // Shared geometric collision queries across all games and native providers.
    class Collision {
        static raycast(from,to,ignoreIndex=-1) {
            return Physics.raycast(from,to,ignoreIndex);
        }
        static sweepSphere(from, to, radius = 0.025, ignoreIndex = -1) {
            return Physics.sweepSphere(from, to, radius, ignoreIndex);
        }
        static lineOfSight(from, to, {radius = 0.01, ignoreIndex = -1} = {}) {
            return Collision.sweepSphere(from, to, radius, ignoreIndex) === null;
        }
        /** Position of the swept center at first contact (not the surface point). */
        static traceSphere(from, to, radius = 0.025, ignoreIndex = -1) {
            const hit = Collision.sweepSphere(from, to, radius, ignoreIndex);
            if (!hit) return null;
            const f = hit.fraction;
            if (typeof f !== "number" || !Number.isFinite(f) || f < 0 || f > 1)
                throw new Error("Physics provider returned invalid hit fraction");
            const point = Object.freeze({
                x: from.x + (to.x - from.x) * f,
                y: from.y + (to.y - from.y) * f,
                z: from.z + (to.z - from.z) * f
            });
            return Object.freeze({hit, point});
        }
    }
    // Engine-side effect providers are optional and replaceable. Weapon muzzle
    // FX are supported by the current native viewport, generic particles aren't.
    class WeaponBallistics {
        static solve({weaponIndex, muzzleLocal, ejectionLocal=null,
                      barrelAxisLocal={x:0,y:0,z:1},
                      eye=null, aimDirection=null, mode="auto",
                      range, radius, adsRadius=null,
                      aimRadius=0.006, ignoreIndex=-1}) {
            // Explicit mode/aim makes this usable for NPC and free weapons.
            // Player input is consulted only for the convenience "auto" mode.
            // A possessed first-person gun must hit what the camera is
            // pointing at even while firing from the hip. RMB affects the
            // presentation and precision, not which target is selected.
            // Explicit "barrel" remains available for NPCs/turrets.
            const requestedMode=mode;
            // Explicit AI/turret fire is independent of Player and Camera.
            // Query native view/input only for the user-controlled auto mode.
            const firstPerson=mode==="auto" && Camera.mode()==="first_person";
            const adsActive=firstPerson && Input.mouseDown(1);
            if(mode==="auto")mode=firstPerson?"sight":"barrel";
            if(mode!=="sight" && mode!=="barrel")
                throw new RangeError("Ballistics mode must be auto/sight/barrel");
            // Keep the hip-fire projectile radius separate from the precise
            // ADS radius. Explicit sight-mode callers may opt into ADS.
            if(mode==="sight" && adsRadius!==null &&
               (adsActive || requestedMode==="sight"))
                radius=adsRadius;
            const finitePoint=p=>p && [p.x,p.y,p.z].every(Number.isFinite);
            const unit=p=>{
                const length=Math.hypot(p.x,p.y,p.z);
                if(!Number.isFinite(length) || length<1e-6)
                    throw new RangeError("Ballistics direction must be nonzero");
                return {x:p.x/length,y:p.y/length,z:p.z/length};
            };
            if(!Number.isSafeInteger(weaponIndex) || weaponIndex<0 ||
               !finitePoint(muzzleLocal) || !finitePoint(barrelAxisLocal) ||
               (ejectionLocal!==null && !finitePoint(ejectionLocal)) ||
               (eye!==null && !finitePoint(eye)) ||
               (aimDirection!==null && !finitePoint(aimDirection)) ||
               !Number.isFinite(range) ||
               range<=0 || range>100000 || !Number.isFinite(radius) ||
               radius<=0 || radius>5 || !Number.isFinite(aimRadius) ||
               aimRadius<=0 || aimRadius>5 ||
               !Number.isSafeInteger(ignoreIndex) || ignoreIndex< -1)
                throw new TypeError("Invalid shared weapon ballistic parameters");
            const muzzle=Scene.attachmentPoint(weaponIndex,muzzleLocal);
            if(!finitePoint(muzzle))return null; // no guessed origin
            const axis=unit(barrelAxisLocal);
            const barrelSample=Scene.attachmentPoint(weaponIndex,{
                x:muzzleLocal.x+axis.x*.10,
                y:muzzleLocal.y+axis.y*.10,
                z:muzzleLocal.z+axis.z*.10
            });
            if(!finitePoint(barrelSample))return null;
            const barrel=unit({
                x:barrelSample.x-muzzle.x,
                y:barrelSample.y-muzzle.y,
                z:barrelSample.z-muzzle.z
            });
            // Ejection port and both local basis directions are sampled from
            // the SAME weapon transform as muzzle. They remain correct when
            // the pistol turns, holsters, goes free or switches to ADS.
            const eject=ejectionLocal===null?null:
                Scene.attachmentPoint(weaponIndex,ejectionLocal);
            if(ejectionLocal!==null && !finitePoint(eject))return null;
            const relative=(p,axis)=>Scene.attachmentPoint(weaponIndex,{
                x:p.x+axis.x*.10,y:p.y+axis.y*.10,z:p.z+axis.z*.10
            });
            const ejectX=eject?relative(ejectionLocal,{x:1,y:0,z:0}):null;
            const ejectY=eject?relative(ejectionLocal,{x:0,y:1,z:0}):null;
            if(eject && (!finitePoint(ejectX)||!finitePoint(ejectY)))
                return null;
            const casing=eject?{
                origin:eject,
                right:unit({x:ejectX.x-eject.x,y:ejectX.y-eject.y,
                            z:ejectX.z-eject.z}),
                up:unit({x:ejectY.x-eject.x,y:ejectY.y-eject.y,
                         z:ejectY.z-eject.z})
            }:null;
            if(mode==="sight") {
                // Require an explicit NPC aim or the native possessed camera.
                if(eye===null)eye=Player.eye();
                if(aimDirection===null) {
                    const {yaw,pitch}=Camera.angles();
                    aimDirection={
                        x:Math.sin(yaw)*Math.cos(pitch),
                        y:Math.sin(pitch),
                        z:Math.cos(yaw)*Math.cos(pitch)
                    };
                }
            } else {
                // A free weapon does not need a player camera; its authoritative
                // scene/physics muzzle transform is already available here.
                if(eye===null)eye=muzzle;
                if(aimDirection===null)aimDirection=barrel;
            }
            if(!finitePoint(eye) || !finitePoint(aimDirection))
                throw new TypeError("Invalid eye or aim direction");
            const aim=unit(aimDirection);
            const sightEnd={
                x:eye.x+aim.x*range,y:eye.y+aim.y*range,
                z:eye.z+aim.z*range};
            const cameraHit=mode==="sight"?Collision.sweepSphere(
                eye,sightEnd,aimRadius,ignoreIndex):null;
            const fraction=cameraHit && Number.isFinite(cameraHit.fraction)?
                Math.max(0,Math.min(1,cameraHit.fraction)):1;
            const aimPoint=mode==="sight"?{
                x:eye.x+aim.x*range*fraction,
                y:eye.y+aim.y*range*fraction,
                z:eye.z+aim.z*range*fraction
            }:{
                x:muzzle.x+barrel.x*range,
                y:muzzle.y+barrel.y*range,
                z:muzzle.z+barrel.z*range
            };
            const toward={
                x:aimPoint.x-muzzle.x,
                y:aimPoint.y-muzzle.y,
                z:aimPoint.z-muzzle.z};
            // A camera obstruction immediately behind the muzzle must not
            // cause a backward muzzle ray or originate through a wall.
            const forward=mode==="barrel"?barrel:
                (Math.hypot(toward.x,toward.y,toward.z)>0.001?
                    unit(toward):aim);
            const muzzleDistance=Math.hypot(toward.x,toward.y,toward.z);
            const blocked=mode==="sight" &&
                (muzzleDistance<0.001 ||
                 forward.x*aim.x+forward.y*aim.y+forward.z*aim.z<=0);
            const end=blocked?muzzle:(cameraHit?aimPoint:{
                x:muzzle.x+forward.x*range,
                y:muzzle.y+forward.y*range,
                z:muzzle.z+forward.z*range});
            const hit=blocked?null:Collision.sweepSphere(
                muzzle,end,radius,ignoreIndex);
            return Object.freeze({
                muzzle, aimPoint, direction:forward, barrelDirection:barrel,
                hit, blocked, mode, casing,
                effects:{
                    flash:{origin:muzzle,direction:barrel},
                    smoke:{origin:muzzle,direction:barrel},
                    casing
                },
                cameraRay:{from:eye,to:sightEnd},
                shotRay:{from:muzzle,to:end}
            });
        }
    }

    // Universal weapon-shot service. Each gun supplies a ballistic solution;
    // the engine owns short-lived muzzle VFX and persistent physical brass.
    // No game or weapon prefab contains duplicate ejection logic.
    class Particles {
        static available(effect = "weapon.muzzle") {
            return effect === "weapon.muzzle"
                ? typeof engine.weaponfx?.effects?.emitShot === "function"
                : typeof engine.particles?.emit === "function";
        }
        static emit(effect, {position, direction = {x:0,y:1,z:0}, scale = 1} = {}) {
            if (typeof effect !== "string" ||
                !/^[a-zA-Z0-9_.:-]{1,96}$/.test(effect))
                throw new TypeError("Particle effect identifier is invalid");
            Contracts.vector(position,"Particle position");
            Contracts.vector(direction,"Particle direction");
            Contracts.number(scale,"Particle scale",Number.MIN_VALUE,100);
            const len = Math.hypot(direction.x,direction.y,direction.z);
            if (len < 1e-6) throw new RangeError("Particle direction must be nonzero");
            const dir = {x:direction.x/len,y:direction.y/len,z:direction.z/len};
            if (effect === "weapon.muzzle") {
                if (scale !== 1)
                    throw new RangeError("Native muzzle effect has no scale support");
                if (!Particles.available(effect))
                    throw new Error("Native muzzle effect provider unavailable");
                return Boolean(engine.weaponfx.effects.emitShot(position.x,position.y,position.z,
                    dir.x,dir.y,dir.z));
            }
            if (!Particles.available(effect))
                throw new Error("Generic particle provider unavailable: "+effect);
            // Optional provider ABI: emit({effect,position,direction,scale}).
            return Boolean(engine.particles.emit({effect,position,direction:dir,scale}));
        }
        static tryEmit(effect, options = {}) {
            if (!Particles.available(effect)) return false;
            return Particles.emit(effect, options);
        }
    }

    // Pure deterministic viewmodel recoil. Sampling is frame-rate independent:
    // the shot impulse is authored once; no random camera noise and no
    // modification of the physical muzzle or the authoritative aim ray.
    class WeaponRecoilMotion {
        #time=1;
        #back=0;
        #arm=0;
        #snap=.042;
        #accent=.038;
        #recovery=.135;
        #aftershock=.20;

        trigger({weaponBackMeters=0,armImpulse=0,
                 snapSeconds=.045,accentSeconds=.035,
                 settleSeconds=.14,aftershock=.18}={}) {
            const values=[weaponBackMeters,armImpulse,snapSeconds,
                          accentSeconds,settleSeconds,aftershock];
            if(!values.every(Number.isFinite) ||
               weaponBackMeters<0 || weaponBackMeters>.06 ||
               armImpulse<0 || armImpulse>1 ||
               snapSeconds<.015 || snapSeconds>.2 ||
               accentSeconds<.01 || accentSeconds>.2 ||
               settleSeconds<.06 || settleSeconds>.8 ||
               aftershock<0 || aftershock>1)
                throw new RangeError("Invalid authored recoil motion");
            // Closely spaced shots compound but cannot push the gun through
            // the camera or accumulate unlimited elbow bending.
            const existing=this.sample();
            this.#back=Math.min(.048,weaponBackMeters+existing.backMeters*.35);
            this.#arm=Math.min(1,armImpulse+existing.armImpulse*.30);
            this.#snap=snapSeconds;
            this.#accent=accentSeconds;
            this.#recovery=settleSeconds;
            this.#aftershock=aftershock;
            this.#time=0;
        }

        advance(dt) {
            if(!Number.isFinite(dt) || dt<0 || dt>.5)
                throw new RangeError("Invalid weapon recoil frame time");
            this.#time=Math.min(this.#time+dt,
                this.#snap+this.#accent+this.#recovery+.3);
            return this.sample();
        }

        sample() {
            const t=this.#time;
            const snap=this.#snap,accent=this.#accent;
            const finish=snap+accent+this.#recovery;
            if(t>=finish)return {backMeters:0,armImpulse:0,active:false};
            let shape;
            if(t<snap) {
                const u=t/snap;
                shape=1-(1-u)*(1-u)*(1-u);
            } else if(t<snap+accent) {
                const u=(t-snap)/accent;
                shape=1-.16*u;
            } else {
                const u=(t-snap-accent)/this.#recovery;
                // A critically settled recoil with one small, authored
                // aftershock, not continuous random jitter.
                const spring=Math.pow(1-u,2)*(1+.18*Math.sin(2*Math.PI*u));
                shape=.84*Math.max(0,spring);
            }
            const armShape=shape*(1+this.#aftershock*.10*
                Math.sin(Math.min(1,t/finish)*Math.PI*3));
            return {
                backMeters:this.#back*shape,
                armImpulse:Math.min(1,Math.max(0,this.#arm*armShape)),
                active:true
            };
        }

        reset() {this.#time=1;this.#back=0;this.#arm=0;}
    }
    class WeaponEffects {
        static #shots=0;
        static #recoilMotion=new WeaponRecoilMotion();
        static advanceMotion(dt) {return WeaponEffects.#recoilMotion.advance(dt);}
        static viewMotion() {return WeaponEffects.#recoilMotion.sample();}
        static resetMotion() {WeaponEffects.#recoilMotion.reset();}
        static #casingFailures=0;
        // Registry only observes Jolt contact events; the actual shell remains
        // a persistent dynamic Scene entity, never a visual-only particle.
        static #shellContacts=new Map();
        static #contactClock=0;
        static #contactSounds=0;
        static #rollSounds=0;
        static #contactSubscription=null;
        static #contactStop=null;
        static #ensureContacts() {
            if (WeaponEffects.#contactSubscription || typeof engine.on!=="function")
                return;
            WeaponEffects.#contactSubscription=Physics.onContacts((events,dt)=>
                WeaponEffects.#processContacts(events,dt));
            WeaponEffects.#contactStop=Events.on("stop",()=>{
                WeaponEffects.#shellContacts.clear();
                WeaponEffects.#contactSubscription=null;
                WeaponEffects.#contactStop?.();
                WeaponEffects.#contactStop=null;
            });
        }
        static #playShell(shell,index,clip,variation,gain) {
            // Each impact is emitted at the *actual* Jolt casing position.
            // In an audio ABI-1 environment the 2D NAC fallback still works.
            if(typeof engine.scene?.entities?.objects==="function" &&
               typeof Audio.playNacAt==="function") {
                const position=Scene.get(index)?.position;
                if(position)
                    return Audio.playNacAt(shell.audio,clip,
                        position,variation,gain);
            }
            return Audio.playNac(shell.audio,clip,variation,gain);
        }

        static #processContacts(events,dt) {
            if(!Number.isFinite(dt)||dt<=0||dt>.5)return;
            WeaponEffects.#contactClock+=dt;
            for(const event of events) {
                if(event.phase!==0 || event.response!==2)continue;
                const index=WeaponEffects.#shellContacts.has(event.first)?
                    event.first:event.second;
                const shell=WeaponEffects.#shellContacts.get(index);
                if(!shell)continue;
                const time=WeaponEffects.#contactClock;
                if(time-shell.lastContact<.075)continue;
                shell.lastContact=time;
                ++shell.contacts;
                // The contact stream identifies real rigid-body collisions,
                // including the native ground (-1). An initial impact rings
                // strongly; subsequent bounces use lighter NAC variations.
                const category=shell.contacts===1?"shell_contact_hard":
                    shell.contacts===2?"shell_contact_medium":
                    shell.contacts===3?"shell_contact_small":
                    "shell_contact_soft";
                const gain=shell.contacts===1?.48:
                    shell.contacts===2?.34:shell.contacts===3?.24:.14;
                try {
                    WeaponEffects.#playShell(shell,index,category,
                        (shell.serial+shell.contacts)%7,gain);
                    ++WeaponEffects.#contactSounds;
                } catch(error) {
                    if(shell.contacts===1)
                        Logger.warn("[engine.weaponfx] shell impact NAC: "+String(error));
                }
                // Rolling is a separate sound layer; only after multiple
                // actual ground/bodies contacts, not at spawn.
                if(!shell.rolled && shell.contacts>=3) {
                    shell.rolled=true;
                    try {
                        WeaponEffects.#playShell(shell,index,"shell_roll",
                            shell.serial%3,.14);
                        ++WeaponEffects.#rollSounds;
                    } catch(error) {
                        Logger.warn("[engine.weaponfx] shell roll NAC: "+String(error));
                    }
                }
            }
        }

        /** Compatibility for tools without the engine event bus.
         *  Runtime contacts are consumed only by Physics.onContacts. */
        static update(dt) {
            if (WeaponEffects.#contactSubscription) return;
            if (typeof engine.on==="function") return;
            WeaponEffects.#processContacts(Physics.pollContacts(256),dt);
        }

        /** Independent recoil: even missing muzzle geometry or VFX must not suppress it. */
        static kick({amplitude = 0.010, duration = 0.10,
                     pitchRadians = 0.045, yawRadians = 0,
                     recoverySeconds = 0.25,
                     weaponBackMeters=0,armImpulse=0,
                     snapSeconds=.045,accentSeconds=.035,
                     settleSeconds=.14,aftershock=.18} = {}) {
            const values = [amplitude,duration,pitchRadians,yawRadians,recoverySeconds];
            if (!values.every(Number.isFinite) || amplitude < 0 || amplitude > 0.2 ||
                duration <= 0 || duration > 2 || Math.abs(pitchRadians) > 0.4 ||
                Math.abs(yawRadians) > 0.4 ||
                recoverySeconds <= 0 || recoverySeconds > 4)
                throw new RangeError("Invalid weapon recoil parameters");
            // Viewmodel impulse uses a spring envelope, distinct from the
            // low-amplitude camera kick. This happens *after* ballistics.
            WeaponEffects.#recoilMotion.trigger({
                weaponBackMeters,armImpulse,snapSeconds,
                accentSeconds,settleSeconds,aftershock
            });
            let shake = false, rotation = false;
            try {
                Camera.shake(amplitude * 0.12, duration);
                shake = true;
            } catch(error) {
                Logger.warn("[engine.weaponfx] camera shake: " + String(error));
            }
            try {
                Camera.recoil(pitchRadians,yawRadians,recoverySeconds);
                rotation = true;
            } catch(error) {
                Logger.warn("[engine.weaponfx] camera recoil: " + String(error));
            }
            return Object.freeze({shake,rotation});
        }

        static fire({shot, casingType="pistol", casingPrefab=null,
                      shellImpulse=1, emitCasing=true,
                      casingAudio=null,
                      casingMaterial={friction:.30,restitution:.42},
                      recoil=null}={}) {
            const point=p=>p && [p.x,p.y,p.z].every(Number.isFinite);
            if(!shot || !point(shot.muzzle) ||
               !point(shot.barrelDirection) ||
               !Number.isFinite(shellImpulse) ||
               shellImpulse<0 || shellImpulse>10 ||
               (casingType!=="pistol"&&casingType!=="rifle") ||
               typeof emitCasing!=="boolean")
                throw new TypeError("Invalid weapon effects descriptor");
            const normal=v=>{
                const length=Math.hypot(v.x,v.y,v.z);
                if(length<1e-6)throw new RangeError("Invalid shot vector");
                return {x:v.x/length,y:v.y/length,z:v.z/length};
            };
            if(emitCasing && !shot.casing)
                throw new Error("Every firearm must provide a valid ejectionLocal socket");
            if(recoil!==null) {
                const {amplitude=.020,duration=.13}=recoil;
                if(!Number.isFinite(amplitude)||amplitude<0||amplitude>.2||
                   !Number.isFinite(duration)||duration<=0||duration>2)
                    throw new RangeError("Invalid camera recoil");
            }
            const barrel=normal(shot.barrelDirection);
            // Real native Viewport muzzle effect, independent of the Jolt
            // scene. Missing optional VFX provider must not suppress brass.
            // Failure of one optional visual provider must not suppress
            // camera recoil or the *physical* brass ejection.
            let particles=false;
            try {
                particles=Particles.tryEmit("weapon.muzzle", {
                    position:shot.muzzle, direction:barrel
                });
            } catch(error) {
                Logger.warn("[engine.weaponfx] muzzle VFX: "+String(error));
            }
            // Recoil is independent from smoke, mesh/material loading and
            // cartridge spawning. Never leave it behind a throwing branch.
            if (recoil !== null) WeaponEffects.kick(recoil);
            let casing=null;
            const socket=shot.casing;
            if(emitCasing && socket!==null && socket!==undefined) {
                if(!point(socket.origin) || !point(socket.right) ||
                   !point(socket.up))
                    throw new TypeError("Invalid ejection socket world frame");
                const right=normal(socket.right),up=normal(socket.up);
                // The physically observed ejection port is the source of
                // truth. The gun profile selects pistol/rifle prefab type.
                const prefab=casingPrefab ?? (
                    casingType==="rifle" ?
                        "prefabs/weapon/rifle_shell.nprefab":
                        "prefabs/weapon/pistol_shell.nprefab");
                if(typeof prefab!=="string" ||
                   !/^prefabs\/weapon\/[A-Za-z0-9_-]+\.nprefab$/.test(prefab))
                    throw new TypeError("Unsafe casing prefab reference");
                const variation=(++WeaponEffects.#shots%7-3)*.075;
                const factor=shellImpulse*(1+variation);
                const origin=socket.origin;
                try {
                    const instance=Factory.prefab(prefab,{position:origin});
                    const index=instance.root.index;
                    const vector={
                        x:(right.x*.018+up.x*.012-barrel.x*.003)*factor,
                        y:(right.y*.018+up.y*.012-barrel.y*.003)*factor,
                        z:(right.z*.018+up.z*.012-barrel.z*.003)*factor
                    };
                    // Real physics impulse, not a particle "flying casing".
                    // Crucially: NEVER destroy this instance on FX expiry.
                    const configured=Physics.setBodyMaterial(
                        index,casingMaterial);
                    if(!configured && WeaponEffects.#shots===1)
                        Logger.warn("[engine.weaponfx] Physics provider lacks "+
                            "body surface material ABI; casing bounces use provider defaults");
                    Physics.applyImpulse(index,vector);
                    casing=instance;
                    if(casingAudio) {
                        // Bound the audio registry without deleting physics
                        // actors. Stale records simply age out of sound tracking.
                        if(WeaponEffects.#shellContacts.size>=2048) {
                            const oldest=WeaponEffects.#shellContacts.keys().next().value;
                            WeaponEffects.#shellContacts.delete(oldest);
                        }
                        WeaponEffects.#shellContacts.set(index,{
                            audio:casingAudio,serial:WeaponEffects.#shots,
                            contacts:0,lastContact:-1,rolled:false
                        });
                        WeaponEffects.#ensureContacts();
                    }
                } catch(error) {
                    ++WeaponEffects.#casingFailures;
                    // Failure remains observable, but a missing optional
                    // casing must not abort input, the firing animation, or
                    // the recoil and muzzle flash on subsequent frames.
                    Logger.warn("[engine.weaponfx] physical casing: "+String(error));
                }
            } else ++WeaponEffects.#shots;
            return Object.freeze({particles,casing,
                serial:WeaponEffects.#shots,
                casingFailed:emitCasing && casing===null});
        }

        static stats() {
            return Object.freeze({shots:WeaponEffects.#shots,
                casingFailures:WeaponEffects.#casingFailures,
                trackedShells:WeaponEffects.#shellContacts.size,
                contactSounds:WeaponEffects.#contactSounds,
                rollSounds:WeaponEffects.#rollSounds});
        }
    }

    class Session {
        static pause(paused) { return engine.session.lifecycle.pause(paused); }
        static scene() { return engine.session.scenes.scene(); }
        static loadScene(path) { return engine.session.scenes.loadScene(path); }
        static quit() { return engine.session.lifecycle.quit(); }
        static restart() { return engine.session.lifecycle.restart(); }
    }

    // Game-agnostic UI renderer. Project scripts own every semantic layer:
    // quests, health, inventory, notifications, and screen advertising.
    // Only engine.ui.documents.apply(document) crosses into native C++.
    class UI {
        static #layers = new Map();
        static #RootType = null;
        static #surfaces = new Map();
        static #assetRoots = new Map();
        static #assetWatchEnabled = false;
        static #assetWatchElapsed = 0;
        static #animationClock = 0;

        // The default authoring implementation is loaded from
        // scripts/ui/UI.js before any project script. A project can override
        // that root module without replacing the native UI provider.
        static registerRoot(RootType) {
            if (UI.#RootType !== null)
                throw new Error("UI root script has already been registered");
            if (typeof RootType !== "function" ||
                typeof RootType.prototype.mount !== "function" ||
                typeof RootType.prototype.unmount !== "function")
                throw new TypeError("UI root must implement mount/unmount");
            UI.#RootType = RootType;
        }

        static createRoot(layer = "default") {
            if (UI.#RootType === null)
                throw new Error("scripts/ui/UI.js is not registered");
            return new UI.#RootType(layer);
        }

        /** Build a generic UI document rendered on an Entity's front face. */
        static createSurface(entityId, options = {}) {
            if (!UI.#RootType)
                throw new Error("scripts/ui/UI.js is not registered");
            return new UI.#RootType(options.layer ?? "surface",
                {...options,entityId:String(entityId),target:"world"});
        }

        /**
         * Instantiate a declarative .nui UI asset from the shared/project VFS.
         * Project assets override identically named engine assets. The returned
         * UIRoot is editable at runtime and can register click/event handlers.
         * Call .mount() when bindings have been attached.
         *
         * UI.loadAsset("ui/main_menu.nui", {layer:"menu"})
         * UI.loadAsset("ui/terminal.nui", {layer:"terminal",
         *     entityId:targetEntityId})
         */
        static loadAsset(path, options = {}) {
            if(typeof path!=="string" ||
               !/^ui\/[a-zA-Z0-9_.\/-]+\.nui$/.test(path) ||
               path.split("/").some(segment=>segment==="." || segment===".." ||
                                              segment.length===0))
                throw new TypeError("UI assets must be relative ui/*.nui files");
            const source=engine.ui.documents.readAsset(path);
            if(typeof source!=="string" || source.length>262144)
                throw new TypeError("Invalid UI asset contents");
            const doc=JSON.parse(source);
            if(!doc || !["northstar.ui/1","northstar.ui/2"].includes(doc.schema) ||
               !["screen","world"].includes(doc.target) ||
               !Array.isArray(doc.nodes) || doc.nodes.length>256)
                throw new TypeError("Unsupported or malformed NorthStar UI document");
            // UI/2: design tokens and reusable named styles are source-level
            // authoring metadata, never new engine.ui ABI or gameplay logic.
            const version2=doc.schema==="northstar.ui/2";
            const tokens=version2?(doc.tokens??{}):{};
            const styles=version2?(doc.styles??{}):{};
            const plain=value=>value!==null && typeof value==="object" &&
                !Array.isArray(value);
            const ident=/^[a-zA-Z][a-zA-Z0-9_.-]{0,63}$/;
            if(!plain(tokens)||!plain(styles) ||
               Object.keys(tokens).length>128 ||
               Object.keys(styles).length>128)
                throw new TypeError("Invalid UI token or style dictionary");
            for(const [key,value] of Object.entries(tokens))
                if(!ident.test(key) ||
                   !(typeof value==="string" || typeof value==="boolean" ||
                     (typeof value==="number" && Number.isFinite(value))))
                    throw new TypeError("Invalid UI design token: "+key);
            for(const [key,rule] of Object.entries(styles))
                if(!ident.test(key) || !plain(rule) ||
                   Object.keys(rule).length>48)
                    throw new TypeError("Invalid UI style: "+key);
            const valueOf=(value)=>{
                let resolved=value;
                if(plain(value) && Object.keys(value).length===1 &&
                   Object.prototype.hasOwnProperty.call(value,"token")) {
                    const name=value.token;
                    if(typeof name!=="string" ||
                       !Object.prototype.hasOwnProperty.call(tokens,name))
                        throw new TypeError("Unknown UI design token: "+name);
                    resolved=tokens[name];
                }
                if(typeof resolved==="number" &&
                   (!Number.isFinite(resolved) || Math.abs(resolved)>1000000))
                    throw new TypeError("UI numeric property exceeds runtime bounds");
                return resolved;
            };
            const layer=options.layer??doc.layer??"asset";
            const surface=doc.target==="world";
            if(surface && options.entityId===undefined)
                throw new TypeError("World UI asset requires options.entityId");
            if(!surface && options.entityId!==undefined)
                throw new TypeError("Screen UI asset does not accept entityId");
            const root=options.__reuseRoot??(surface?
                UI.createSurface(options.entityId,{
                    layer,width:doc.width??512,height:doc.height??256,
                    worldWidth:doc.worldWidth??1.8,
                    worldHeight:doc.worldHeight??1
                }):UI.createRoot(layer));
            const components=doc.components??{};
            const actions=doc.actions??[];
            const animations=doc.animations??[];
            const breakpoints=doc.breakpoints??[];
            if(!plain(components) || Object.keys(components).length>64 ||
               !Array.isArray(actions)||actions.length>128 ||
               !Array.isArray(animations)||animations.length>128 ||
               !Array.isArray(breakpoints)||breakpoints.length>64)
                throw new TypeError("Invalid XML component/action/animation/breakpoint metadata");
            const viewport=typeof engine.ui.viewport.viewportSize==="function"?
                engine.ui.viewport.viewportSize():{};
            const viewportWidth=options.viewportWidth??viewport?.width??1920;
            const active=breakpoints.filter(rule=>
                viewportWidth>=rule.minWidth && viewportWidth<=rule.maxWidth);
            const responsive=new Map();
            for(const rule of active)
                for(const [id,overrides] of Object.entries(rule.overrides))
                    responsive.set(id,{...(responsive.get(id)??{}),...overrides});
            const elements=[];
            for(const node of doc.nodes) {
                    if(!node || typeof node!=="object" ||
                       !Array.isArray(Object.keys(node)) ||
                       !["panel","text","button","image","input"].includes(node.type))
                        throw new TypeError("Unsupported UI asset element");
                    const style=node.style;
                    if(style!==undefined &&
                       (typeof style!=="string" || !ident.test(style) ||
                        !Object.prototype.hasOwnProperty.call(styles,style)))
                        throw new TypeError("Unknown UI style: "+String(style));
                    const component=node.component;
                    let inherited={};
                    if(component!==undefined){
                        if(typeof component!=="string" || !ident.test(component) ||
                           !plain(components[component]) ||
                           components[component].type!==node.type)
                            throw new TypeError("Unknown reusable .nui component: "+component);
                        inherited=components[component].props??{};
                        if(!plain(inherited))
                            throw new TypeError("UI component props must be an object");
                    }
                    const props={...inherited,...(style?styles[style]:{}),
                        ...(node.props??{}),...(responsive.get(node.id)??{})};
                    for(const [name,value] of Object.entries(props))
                        props[name]=valueOf(value);
                    elements.push({type:node.type,id:node.id,props,parent:node.parent??""});
                }
            const names=new Set(elements.map(node=>node.id));
            const animationsByName=new Map();
            for(const spec of animations) {
                if(typeof spec.name!=="string" || !ident.test(spec.name) ||
                   animationsByName.has(spec.name) || !names.has(spec.id) ||
                   !["x","y","width","height","font_scale"].includes(spec.property) ||
                   !Number.isFinite(spec.from) || !Number.isFinite(spec.to) ||
                   !Number.isFinite(spec.duration)||spec.duration<=0||spec.duration>30 ||
                   !["linear","easeOutCubic","easeInOut"].includes(spec.easing) ||
                   !["manual","mount"].includes(spec.trigger))
                    throw new TypeError("Invalid .nui animation definition");
                animationsByName.set(spec.name,spec);
            }
            for(const action of actions) {
                if(!names.has(action.id) ||
                   !["click","press","change"].includes(action.event) ||
                   !/^[a-zA-Z_][a-zA-Z0-9_.:-]{0,95}$/.test(action.emit) ||
                   (action.animation && !animationsByName.has(action.animation)))
                    throw new TypeError("Invalid declarative UI action");
            }
            // Existing callbacks survive hot reload. Remove only callbacks
            // generated by this XML document, never user-installed callbacks.
            const previous=UI.#assetRoots.get(root);
            if(previous?.boundActions)
                for(const [action,callback] of previous.boundActions)
                    root.off(action.id,action.event,callback);
            if(options.__reuseRoot)
                root.reloadFromAsset(elements);
            else root.batch(editor=>{
                for(const node of elements)
                    editor.add(node.type,node.id,node.props,node.parent);
            });
            const boundActions=[];
            for(const action of actions) {
                const callback=event=>{
                    if(action.animation)UI.playAssetAnimation(root,action.animation);
                    Events.emit(action.emit,{
                        source:"nui",id:action.id,layer,event
                    });
                };
                root.on(action.id,action.event,callback);
                boundActions.push([action,callback]);
            }
            UI.#assetRoots.set(root,{path,source,
                options:{layer,entityId:options.entityId,
                    viewportWidth:options.viewportWidth},
                rejected:null,boundActions,animationsByName,
                activeAnimations:previous?.activeAnimations??[],
                startAnimations:!options.__reuseRoot || previous?.startAnimations,
                width:viewportWidth});
            UI.#startAssetWatch();
            return root;
        }

        static #startAssetWatch() {
            if(UI.#assetWatchEnabled || typeof engine.on!=="function")return;
            UI.#assetWatchEnabled=true;
            Events.on("ui.update",dt=>{
                UI.advanceAnimations(dt);
                UI.#assetWatchElapsed+=Math.min(0.1,Math.max(0,Number(dt)||0));
                if(UI.#assetWatchElapsed<0.5)return;
                UI.#assetWatchElapsed=0;
                const hot=typeof engine.ui.documents.hotReloadEnabled==="function" &&
                    engine.ui.documents.hotReloadEnabled()===true;
                if(hot)UI.reloadChangedAssets();
                // Responsive rules must be reapplied after viewport resize,
                // independent of whether hot reload is enabled.
                if(typeof engine.ui.viewport.viewportSize==="function") {
                    const width=engine.ui.viewport.viewportSize()?.width;
                    if(Number.isFinite(width))for(const [root,meta] of UI.#assetRoots)
                        if(root.mounted && meta.width!==width) {
                            UI.loadAsset(meta.path,{
                                ...meta.options,__reuseRoot:root});
                        }
                }
            });
        }
        /** Released by UIRoot.dispose(), including packaged Player instances. */
        static releaseAssetRoot(root) {
            return UI.#assetRoots.delete(root);
        }
        static playAssetAnimation(root,name) {
            const meta=UI.#assetRoots.get(root);
            const animation=meta?.animationsByName.get(name);
            if(!animation)return false;
            meta.activeAnimations=meta.activeAnimations.filter(a=>a.name!==name);
            meta.activeAnimations.push({...animation,elapsed:0});
            root.setTransient(animation.id,{[animation.property]:animation.from});
            return true;
        }
        /** Deterministic time-step; called on ui.update, available to tests. */
        static advanceAnimations(dt) {
            const elapsed=Math.max(0,Math.min(0.1,Number(dt)||0));
            for(const [root,meta] of UI.#assetRoots) {
                if(!root.mounted)continue;
                if(meta.startAnimations) {
                    meta.startAnimations=false;
                    for(const spec of meta.animationsByName.values())
                        if(spec.trigger==="mount")UI.playAssetAnimation(root,spec.name);
                }
                if(!elapsed || !meta.activeAnimations.length)continue;
                const next=[];
                root.batch(()=>{
                    for(const spec of meta.activeAnimations){
                        spec.elapsed=Math.min(spec.duration,spec.elapsed+elapsed);
                        const t=spec.elapsed/spec.duration;
                        const eased=spec.easing==="easeOutCubic"?1-(1-t)**3:
                            spec.easing==="easeInOut"?t*t*(3-2*t):t;
                        root.setTransient(spec.id,{
                            [spec.property]:spec.from+(spec.to-spec.from)*eased
                        });
                        if(t<1)next.push(spec);
                    }
                });
                meta.activeAnimations=next;
            }
        }
        static reloadChangedAssets() {
            let updated=0;
            for(const [root,meta] of UI.#assetRoots) {
                if(!root.mounted && root.size===0){
                    UI.#assetRoots.delete(root);continue;
                }
                let next;
                try{next=engine.ui.documents.readAsset(meta.path);}
                catch(err){continue;}
                if(next===meta.source || next===meta.rejected)continue;
                try{
                    UI.loadAsset(meta.path,{...meta.options,__reuseRoot:root});
                    updated++;
                }catch(err){
                    meta.rejected=next;
                    try{Logger.warn("UI reload rejected: "+String(err));}catch(_){}
                }
            }
            return updated;
        }
        static hasSurface(entityId) { return UI.#surfaces.has(String(entityId)); }
        static setTitle(text) { return engine.ui.viewport.setTitle(text); }
        /** Whether a named presentation layer is currently registered. */
        static hasLayer(layer) { return UI.#layers.has(layer); }

        static #typed(value) {
            if (typeof value === "number") {
                if (!Number.isFinite(value))
                    throw new TypeError("UI numeric property must be finite");
                return {type:"number",value};
            }
            if (typeof value === "boolean") return {type:"bool",value};
            if (typeof value === "string") {
                if (/^#[0-9a-fA-F]{8}$/.test(value))
                    return {type:"color",value};
                return {type:"text",value};
            }
            throw new TypeError("UI property must be string, boolean or number");
        }

        static #flush() {
            const nodes = [];
            for (const [layer, items] of UI.#layers) {
                for (const node of items) {
                    const props = {};
                    for (const [key, value] of Object.entries(node.props ?? {}))
                        props[key] = UI.#typed(value);
                    if (layer.length + 1 + node.id.length > 96 ||
                        (node.parent && layer.length + 1 + node.parent.length > 96))
                        throw new RangeError("UI scoped node identifier exceeds 96 characters");
                    if (nodes.length >= 256)
                        throw new RangeError("UI document exceeds 256 elements");
                    nodes.push({
                        id: layer + "." + node.id,
                        parent: node.parent ? layer + "." + node.parent : "",
                        type: node.type,
                        props
                    });
                }
            }
            return engine.ui.documents.apply({target:"screen",nodes});
        }

        static mount(layer, elements) {
            if (typeof layer !== "string" ||
                !/^[a-zA-Z][a-zA-Z0-9_-]{0,47}$/.test(layer) ||
                !Array.isArray(elements) || elements.length > 256)
                throw new TypeError("UI.mount requires a layer and bounded element array");
            // Clone user data so later mutation cannot silently desynchronize
            // the native document from its script-owned source.
            const owned = elements.map(node => {
                if (!node || typeof node !== "object" ||
                    !/^[a-zA-Z][a-zA-Z0-9_.-]{0,63}$/.test(node.id) ||
                    (node.parent !== undefined && node.parent !== "" &&
                     !/^[a-zA-Z][a-zA-Z0-9_.-]{0,63}$/.test(node.parent)) ||
                    !["panel","text","button","image","input"].includes(node.type))
                    throw new TypeError("UI node descriptor is invalid");
                return {
                    id: node.id, type:node.type, parent:node.parent ?? "",
                    props:{...node.props}
                };
            });
            // Validate the public authoring tree before it reaches the native
            // provider. Errors point to the actual layer and offending node.
            const known = new Set();
            for (const node of owned) {
                if (known.has(node.id))
                    throw new Error('UI layer "' + layer +
                        '" declares duplicate element "' + node.id + '"');
                if (node.parent && !known.has(node.parent))
                    throw new Error('UI layer "' + layer + '" element "' +
                        node.id + '" must follow its parent "' + node.parent + '"');
                known.add(node.id);
            }
            const previous = UI.#layers.get(layer);
            const unchanged = previous && previous.length === owned.length &&
                previous.every((item,i) => {
                    const next = owned[i];
                    if (item.id !== next.id || item.parent !== next.parent ||
                        item.type !== next.type) return false;
                    const keys = Object.keys(item.props);
                    const nextKeys = Object.keys(next.props);
                    return keys.length === nextKeys.length &&
                        keys.every(key => Object.prototype.hasOwnProperty.call(
                            next.props,key) && Object.is(item.props[key],next.props[key]));
                });
            if (unchanged) return;
            UI.#layers.set(layer, owned);
            try { return UI.#flush(); }
            catch (error) {
                if (previous === undefined) UI.#layers.delete(layer);
                else UI.#layers.set(layer, previous);
                throw error;
            }
        }

        static unmount(layer) {
            if (!UI.#layers.has(layer)) return;
            const previous = UI.#layers.get(layer);
            UI.#layers.delete(layer);
            try { return UI.#flush(); }
            catch (error) {
                UI.#layers.set(layer, previous);
                throw error;
            }
        }

        static mountSurface(entityId, layer, elements, options = {}) {
            const id=String(entityId);
            if (!/^[0-9]{1,32}$/.test(id))
                throw new TypeError("World UI needs a stable Entity ID");
            const width=options.width ?? 512, height=options.height ?? 256;
            const worldWidth=options.worldWidth ?? 1.8;
            const worldHeight=options.worldHeight ?? 1;
            if (!Number.isInteger(width)||!Number.isInteger(height)||
                width<32||height<32||width>1024||height>1024||
                width*height>524288||
                !Number.isFinite(worldWidth)||!Number.isFinite(worldHeight)||
                worldWidth<0.1||worldWidth>20||
                worldHeight<0.1||worldHeight>20)
                throw new RangeError("Invalid world UI dimensions");
            if(typeof layer!=="string"||
               !/^[a-zA-Z][a-zA-Z0-9_-]{0,47}$/.test(layer)||
               !Array.isArray(elements)||elements.length>256)
                throw new TypeError("World UI needs a valid layer and elements");
            const seen=new Set();
            const nodes=elements.map(node=>{
                if(!node||typeof node.id!=="string"||
                   !/^[a-zA-Z][a-zA-Z0-9_.-]{0,63}$/.test(node.id)||
                   !["panel","text","button","image","input"].includes(node.type)||
                   seen.has(node.id)||(node.parent&&!seen.has(node.parent)))
                    throw new TypeError("Invalid world UI element or parent");
                seen.add(node.id);
                const props={};
                for(const [key,value] of Object.entries(node.props??{}))
                    props[key]=UI.#typed(value);
                return {id:layer+"."+node.id,
                    parent:node.parent?layer+"."+node.parent:"",
                    type:node.type,props};
            });
            const data={target:"world",entityId:id,width,height,
                worldWidth,worldHeight,nodes};
            const signature=JSON.stringify(data);
            if(UI.#surfaces.get(id)===signature)return;
            engine.ui.documents.apply(data);
            UI.#surfaces.set(id,signature);
        }
        static unmountSurface(entityId) {
            const id=String(entityId);
            if(!UI.#surfaces.has(id))return;
            engine.ui.documents.apply({target:"world",entityId:id,nodes:[]});
            UI.#surfaces.delete(id);
        }

        static render(elements) { return UI.mount("default", elements); }

        static clear() {
            if (!UI.#layers.size) return;
            const previous = UI.#layers;
            UI.#layers = new Map();
            try { return UI.#flush(); }
            catch(error) {
                UI.#layers = previous;
                throw error;
            }
        }
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
        static get index() { return engine.player.controller.controlledIndex(); }
        static get entityId() { return engine.player.controller.controlledEntity(); }
        static get hasPawn() { return Player.index >= 0; }
        static requirePawn() {
            const pawn = Scene.findPawn();
            if (!pawn)
                throw new Error("No possessed Pawn. Add a Pawn component and enable Auto Possess Local.");
            return pawn;
        }
        static possess(id) { return engine.player.controller.possess(String(id)); }
        static unpossess() { return engine.player.controller.unpossess(); }
        static sprint(active) { return engine.player.motion.sprint(Boolean(active)); }
        static setIndex(index) { return NorthStar.player.setIndex(index); }
        static get paused() { return NorthStar.player.paused; }
        static setPaused(value) { return NorthStar.player.setPaused(value); }
        static move(x, z) { return engine.player.motion.move(x, z); }
        static motion() { return engine.player.motion.motion(); }
        /** Provider-independent post-physics state, or null before first tick. */
        static motionState() { return engine.player.motion.motionState(); }
        /** Subscribe to player.jump / player.fall / player.land / move events. */
        static on(action, handler) {
            if(typeof action!=="string" ||
               !/^[a-zA-Z][a-zA-Z0-9_.]{0,63}$/.test(action))
                throw new TypeError("Invalid Player action name");
            return Events.on("player."+action,handler);
        }
        static once(action, handler) {
            if(typeof action!=="string" ||
               !/^[a-zA-Z][a-zA-Z0-9_.]{0,63}$/.test(action))
                throw new TypeError("Invalid Player action name");
            return Events.once("player."+action,handler);
        }
        static ground() { return engine.player.motion.ground(); }
        static jump() { return engine.player.motion.jump(); }
        static eye() { return engine.player.motion.eye(); }
        static setEyeHeight(height) { return engine.player.motion.setEyeHeight(height); }
    }

    // Base lifecycle for independent root-script classes. A root class
    // registers once and keeps its mutable state in private instance fields.
    // Single provider-neutral playback contract. No gameplay event routing,
    // no hardcoded asset names and no independent sound implementation.
    class AudioVoice {
        #id;
        #active=true;
        constructor(id) {
            if(!Number.isSafeInteger(id) || id<=0)
                throw new RangeError("Native audio voice requires positive ID");
            this.#id=id;
            Object.freeze(this);
        }
        get id() { return this.#id; }
        get active() { return this.#active; }
        stop() {
            if(!this.#active)return false;
            if(typeof engine.audio?.voices?.stopVoice!=="function")
                throw new Error("Native voice stop capability unavailable");
            engine.audio.voices.stopVoice(this.#id);
            this.#active=false;
            return true;
        }
        setGain(gain) {
            if(!this.#active)throw new Error("Audio voice already stopped");
            Contracts.number(gain,"Voice gain",0,2);
            if(typeof engine.audio?.voices?.setVoiceGain!=="function")
                throw new Error("Native voice gain capability unavailable");
            engine.audio.voices.setVoiceGain(this.#id,gain);
            return this;
        }
        setPosition(position) {
            if(!this.#active)throw new Error("Audio voice already stopped");
            Contracts.vector(position,"Voice position");
            if(typeof engine.audio?.spatial?.setVoicePosition!=="function")
                throw new Error("Native voice spatial capability unavailable");
            engine.audio.spatial.setVoicePosition(this.#id,
                position.x,position.y,position.z);
            return this;
        }
        dispose() { return this.stop(); }
    }

    class Audio {
        static supportsVoices() {
            return typeof engine.audio?.voices?.supportsVoices==="function" &&
                engine.audio.voices.supportsVoices()===true;
        }
        static playVoice(request, options={}) {
            if(!Audio.supportsVoices())
                throw new Error("Native ABI-2 audio voice provider unavailable");
            const descriptor=typeof request==="string"?
                {asset:request,...Contracts.options(options,"Audio options")} :
                Contracts.options(request,"Audio request");
            if(typeof descriptor.asset!=="string" ||
               !descriptor.asset.endsWith(".nac"))
                throw new TypeError("Managed voices require a NAC audio asset");
            const id=Audio.play(descriptor);
            return new AudioVoice(id);
        }

        static play(request, options = {}) {
            const data = typeof request === "string" ?
                {asset:request,...Contracts.options(options,"Audio options")} :
                Contracts.options(request,"Audio request");
            for(const key of Object.keys(data)) {
                if(!["asset","clip","variation","gain","position"].includes(key))
                    throw new RangeError("Unsupported Audio.play option: "+key);
            }
            const {asset,clip=null,variation=0,gain=1,position=null}=data;
            Contracts.vfs(asset,"","audio/");
            if(asset.length>256)
                throw new RangeError("Audio virtual path exceeds provider limit");
            Contracts.number(gain,"Audio gain",0,2);
            if(!Number.isSafeInteger(variation) || variation<0 || variation>1000000)
                throw new RangeError("Audio variation must be 0..1000000");
            if(asset.endsWith(".nac")) {
                if(typeof clip!=="string" || !/^[a-zA-Z0-9_]{1,48}$/.test(clip))
                    throw new TypeError("NAC playback requires valid clip identifier");
                if (position !== null) {
                    Contracts.vector(position,"Audio position");
                    // The host implements spatial NAC through the optional
                    // 7-argument voice ABI. Never invent WAV spatialization.
                    return engine.audio.playback.playNac(asset,clip,variation,gain,
                        position.x,position.y,position.z);
                }
                return engine.audio.playback.playNac(asset,clip,variation,gain);
            }
            if(asset.endsWith(".wav")) {
                if(clip!==null || variation!==0 || gain!==1 || position!==null)
                    throw new RangeError(
                        "Current WAV provider supports no clip, variation or per-voice gain");
                return engine.audio.playback.play(asset);
            }
            throw new TypeError("Audio supports WAV and NAC assets");
        }
        /** Compatibility aliases, delegated to the same single contract. */
        static playWav(path) { return Audio.play(path); }
        static playNac(path,clip,variation=0,gain=1) {
            return Audio.play({asset:path,clip,variation,gain});
        }
        static playNacAt(path,clip,position,variation=0,gain=1) {
            return Audio.play({asset:path,clip,position,variation,gain});
        }
    }

    class EngineCapabilityError extends Error {
        constructor(capability) {
            super("Native engine capability unavailable: "+capability);
            this.name="EngineCapabilityError";
            this.code="CAPABILITY_UNAVAILABLE";
            this.capability=capability;
        }
    }

    class Capabilities {
        static #probes=Object.freeze({
            "ecs.get":()=>typeof engine.ecs?.entities?.get==="function",
            "ecs.has":()=>typeof engine.ecs?.entities?.has==="function",
            "ecs.query":()=>typeof engine.ecs?.entities?.query==="function",
            "ecs.queryMany":()=>typeof engine.ecs?.entities?.queryMany==="function",
            "ecs.setPosition":()=>typeof engine.ecs?.entities?.setPosition==="function",
            "physics.contacts":()=>typeof engine.physics?.contacts?.pollContacts==="function",
            "physics.raycast":()=>typeof engine.physics?.raycast==="function",
            "audio.voices":()=>Audio.supportsVoices(),
            "audio.stopVoice":()=>typeof engine.audio?.voices?.stopVoice==="function",
            "audio.pauseVoice":()=>typeof engine.audio?.voices?.pauseVoice==="function",
            "particles.emit":()=>typeof engine.particles?.emit==="function",
            "particles.muzzle":()=>typeof engine.weaponfx?.effects?.emitShot==="function",
            "assets.open":()=>typeof engine.assets?.files?.open==="function"
        });
        static has(name) {
            if(typeof name!=="string")return false;
            const check=Capabilities.#probes[name];
            if(!check)return false;
            try {return Boolean(check());}
            catch(_) {return false;}
        }
        static require(name) {
            if(!Capabilities.has(name))
                throw new EngineCapabilityError(name);
            return true;
        }
        static list() {
            return Object.freeze(Object.fromEntries(
                Object.keys(Capabilities.#probes).map(name=>
                    [name,Capabilities.has(name)])));
        }
    }

    // Simulation-clock observations. Does not pretend to control native
    // physics/render time scale; all clock values come from engine update dt.
    class Time {
        static #delta = 0;
        static #elapsed = 0;
        static #frame = 0;
        static #disposeUpdate = null;
        static #disposeStop = null;
        static #clients = 0;

        static attach() {
            Time.#clients++;
            if (Time.#disposeUpdate) return;
            if (typeof engine.on !== "function")
                throw new Error("Game update event provider unavailable");
            Time.#disposeUpdate = Events.on("update", dt => {
                if (typeof dt !== "number" || !Number.isFinite(dt) || dt < 0)
                    return;
                Time.#delta = dt;
                Time.#elapsed += dt;
                Time.#frame++;
            });
            Time.#disposeStop = Events.on("stop", () => Time.detach());
        }
        static release() {
            if (Time.#clients>0)Time.#clients--;
            if (Time.#clients===0)Time.detach();
        }
        static detach() {
            Time.#clients=0;
            const update=Time.#disposeUpdate, stop=Time.#disposeStop;
            Time.#disposeUpdate=Time.#disposeStop=null;
            update?.();
            stop?.();
            Time.#delta=0;
        }
        static get delta() { return Time.#delta; }
        static get elapsed() { return Time.#elapsed; }
        static get frame() { return Time.#frame; }
    }

    class ScheduledTask {
        #cancel;
        constructor(cancel) {
            this.#cancel = cancel;
            Object.freeze(this);
        }
        cancel() { return this.#cancel(); }
    }

    // Script-owned deterministic timers. No JS setTimeout, OS wall clock,
    // or unrelated global update subscriptions per scheduled callback.
    class Scheduler {
        #tasks = new Set();
        #disposed = false;
        #error;
        constructor(onError = error => Logger.warn(
            "[engine.scheduler] " + String(error))) {
            if (typeof onError !== "function")
                throw new TypeError("Scheduler error handler must be callable");
            this.#error=onError;
        }
        #add(seconds, callback, repeating) {
            if (this.#disposed) throw new Error("Scheduler was disposed");
            Contracts.number(seconds,"Timer duration",repeating?1e-6:0,86400);
            if (typeof callback !== "function")
                throw new TypeError("Scheduled callback must be a function");
            if (this.#tasks.size >= 1024)
                throw new RangeError("Scheduler task limit exceeded");
            const record={remaining:seconds,period:seconds,callback,repeating};
            this.#tasks.add(record);
            let active=true;
            return new ScheduledTask(()=>{
                if (!active) return false;
                active=false;
                return this.#tasks.delete(record);
            });
        }
        after(seconds, callback) { return this.#add(seconds,callback,false); }
        every(seconds, callback) { return this.#add(seconds,callback,true); }
        update(dt) {
            if (this.#disposed) return;
            Contracts.number(dt,"Scheduler dt",0,1);
            // Bound catch-up work to avoid a stall after a hitch. Each task
            // can fire at most 8 times in one engine frame.
            for (const task of [...this.#tasks]) {
                if (!this.#tasks.has(task)) continue;
                task.remaining -= dt;
                for (let fired=0; task.remaining<=0 && fired<8; ++fired) {
                    if (!task.repeating) this.#tasks.delete(task);
                    else task.remaining += task.period;
                    try { task.callback(); }
                    catch (error) { this.#error(error); this.#tasks.delete(task); }
                    if (!this.#tasks.has(task)) break;
                }
                if (task.repeating && task.remaining<=0)
                    task.remaining=task.period;
            }
        }
        get pending() { return this.#tasks.size; }
        dispose() {
            if (this.#disposed) return false;
            this.#disposed=true;
            this.#tasks.clear();
            return true;
        }
    }

    // FLECS backed by the native engine.ecs bridge. Stable scene IDs are
    // passed to the provider; never substitute recyclable scene indices.
    class ECS {
        static #names=Object.freeze({
            Name:"northstar.name",Position:"northstar.position",
            Visual:"northstar.visual",PhysicsBody:"northstar.body",
            Controller:"northstar.controller",Motion:"northstar.motion",
            Script:"northstar.script"
        });
        static available(operation="get") {
            return typeof engine.ecs?.entities?.[operation]==="function";
        }
        static #id(entity) {
            if(typeof entity==="string" && /^[0-9]{1,20}$/.test(entity))
                return entity;
            // A numeric scene index is NOT a stable ECS identity.
            if(typeof entity==="number")
                throw new TypeError("ECS requires stable ID string, not scene index");
            if(!entity || typeof entity!=="object" ||
               typeof entity.id!=="string")
                throw new TypeError("ECS needs stable Entity ID or Entity handle");
            if(entity.alive===false)
                throw new Error("ECS entity handle is stale");
            return entity.id;
        }
        static #component(name) {
            Contracts.identifier(name,"ECS component");
            return ECS.#names[name]??name;
        }
        static #require(method) {
            const provider=engine.ecs?.entities?.[method];
            if(typeof provider!=="function")
                throw new EngineCapabilityError("ecs."+method);
            return provider.bind(engine.ecs.entities);
        }
        static has(entity,name) {
            return Boolean(ECS.#require("has")(
                ECS.#id(entity),ECS.#component(name)));
        }
        static get(entity,name) {
            return ECS.#require("get")(ECS.#id(entity),ECS.#component(name));
        }
        static set(entity,name,value) {
            // The current provider supports mutable Position only.
            const key=ECS.#component(name);
            if(key!=="northstar.position")
                throw new Error("Native ECS component setter unavailable: "+key);
            Contracts.vector(value,"ECS Position");
            return ECS.#require("setPosition")(ECS.#id(entity),
                value.x,value.y,value.z);
        }
        static remove(_entity,name) {
            throw new Error("Native ECS remove capability unavailable: "+
                ECS.#component(name));
        }
        static query(components) {
            const keys=typeof components==="string"?[components]:components;
            if(!Array.isArray(keys) || !keys.length || keys.length>32)
                throw new TypeError("ECS query needs 1..32 component names");
            const method=ECS.#require("query");
            // Native bridge currently handles one component per call.
            // Set intersection preserves stable IDs and avoids scene rescans.
            const names=keys.map(ECS.#component);
            if(typeof engine.ecs?.entities?.queryMany==="function") {
                const ids=engine.ecs.entities.queryMany(...names);
                if(!Array.isArray(ids))
                    throw new TypeError("Native ECS multi-query returned invalid IDs");
                return Object.freeze(ids.map(String));
            }
            let ids=null;
            for(const name of names) {
                const batch=method(name);
                if(!Array.isArray(batch))
                    throw new TypeError("Native ECS query must return stable IDs");
                const current=new Set(batch.map(String));
                ids=ids===null?current:new Set([...ids].filter(id=>current.has(id)));
                if(!ids.size) break;
            }
            return Object.freeze([...ids]);
        }
    }

    class ScriptContext {
        #entity;
        #owner;
        #disposers=[];
        #disposed=false;
        #scheduler;

        constructor({entity=null,owner=null}={}) {
            this.#entity=entity;
            this.#owner=owner;
            this.#scheduler=new Scheduler();
            Object.freeze(this);
        }
        get entity() { return this.#entity; }
        get owner() { return this.#owner; }
        get active() { return !this.#disposed; }
        get scheduler() { return this.#scheduler; }
        #requireActive() {
            if (this.#disposed) throw new Error("Script context is disposed");
        }
        own(resource) {
            this.#requireActive();
            const cleanup = typeof resource==="function" ? resource :
                resource && typeof resource.dispose==="function" ?
                    () => resource.dispose() :
                resource && typeof resource.cancel==="function" ?
                    () => resource.cancel() :
                resource && typeof resource.uninstall==="function" ?
                    () => resource.uninstall() :
                resource && typeof resource.destroy==="function" ?
                    () => resource.destroy() : null;
            if (!cleanup) throw new TypeError(
                "Owned resource needs dispose(), cancel(), uninstall() or destroy()");
            this.#disposers.push(cleanup);
            return resource;
        }
        listen(event, callback) {
            this.#requireActive();
            return this.own(Events.on(event,callback));
        }
        after(seconds, callback) {
            this.#requireActive();
            return this.#scheduler.after(seconds,callback);
        }
        every(seconds, callback) {
            this.#requireActive();
            return this.#scheduler.every(seconds,callback);
        }
        tick(dt) {
            if (!this.#disposed) this.#scheduler.update(dt);
        }
        dispose() {
            if (this.#disposed) return false;
            this.#disposed=true;
            this.#scheduler.dispose();
            const disposers=this.#disposers.splice(0).reverse();
            for(const cleanup of disposers) {
                try { cleanup(); }
                catch(error) { Logger.warn("[engine.script] cleanup: "+String(error)); }
            }
            return true;
        }
    }

    // OOP execution lifetime shared by player, entity, and project scripts.
    // Engine callbacks never retain disposed scripts; owned resources are
    // released in reverse registration order on stop/uninstall/failure.
    class Script {
        #scope=null;
        #context=null;
        #options;
        #uninstalling=false;

        constructor(options={}) {
            this.#options=Contracts.options(options,"Script options");
        }
        get installed() { return this.#scope!==null; }
        get context() { return this.#context; }
        get entity() { return this.#context?.entity??null; }
        listen(name,handler) {
            if(!this.#context)throw new Error("Install script before subscribing");
            return this.#context.listen(name,handler);
        }
        own(resource) {
            if(!this.#context)throw new Error("Install script before owning resources");
            return this.#context.own(resource);
        }
        after(seconds,callback) {
            if(!this.#context)throw new Error("Install script before scheduling");
            return this.#context.after(seconds,callback);
        }
        every(seconds,callback) {
            if(!this.#context)throw new Error("Install script before scheduling");
            return this.#context.every(seconds,callback);
        }

        #invoke(phase,callback) {
            try { callback(); }
            catch(error) {
                try {
                    Logger.warn("[engine.script] "+this.constructor.name+
                        "."+phase+": "+String(error?.stack??error));
                } catch(_) {}
                this.uninstall();
            }
        }

        install() {
            if(this.installed)throw new Error("Root script already installed");
            Time.attach();
            const scope=Events.scope();
            const context=new ScriptContext(this.#options);
            this.#scope=scope;
            this.#context=context;
            try {
                scope.on("start",()=>this.#invoke("start",()=>this.start()));
                scope.on("update",dt=>this.#invoke("update",()=>{
                    context.tick(dt);
                    this.update(dt);
                }));
                scope.on("stop",()=>{
                    this.#invoke("stop",()=>this.stop());
                    this.uninstall();
                });
            } catch(error) {
                this.uninstall();
                throw error;
            }
            return this;
        }

        uninstall() {
            if(!this.#scope || this.#uninstalling)return false;
            this.#uninstalling=true;
            const scope=this.#scope, context=this.#context;
            try {
                scope.dispose();
                // onDestroy runs while context is still readable.
                try { this.onDestroy(); }
                catch(error) {
                    try { Logger.warn("[engine.script] onDestroy: "+String(error)); }
                    catch(_) {}
                }
                context?.dispose();
                Time.release();
            } finally {
                this.#scope=null;
                this.#context=null;
                this.#uninstalling=false;
            }
            return true;
        }
        start() {}
        update(_dt) {}
        stop() {}
        onDestroy() {}
    }

    class PlayerScript extends Script {
        get enabled() { return Player.enabled; }
        get active() { return Player.enabled && Player.index>=0; }
        get playing() { return this.active && !Player.paused; }
        get options() { return Player.enabled ? Player.config : null; }
    }

    // Entity is a stable, non-owning handle supplied by scene or Factory.
    // Native automatic attachment to Scene ECS components is a separate
    // host capability; no fake getComponent() is installed here.
    class EntityScript extends Script {
        constructor({entity,owner=null}={}) {
            if(!entity || typeof entity.id!=="string" ||
               !Number.isSafeInteger(entity.index) ||
               typeof entity.alive!=="boolean")
                throw new TypeError("EntityScript requires a live Entity handle");
            if(!entity.alive)throw new Error("Cannot attach to a dead Entity");
            super({entity,owner:owner??entity});
        }
        get entity() {
            const entity=super.entity;
            if(!entity)throw new Error("EntityScript is not installed");
            if(!entity.alive)throw new Error("Bound Entity was destroyed");
            return entity;
        }
        start() { this.onStart(); }
        update(dt) {
            if(!this.entity.alive)return;
            this.onUpdate(dt);
        }
        stop() { this.onStop(); }
        onStart() {}
        onUpdate(_dt) {}
        onStop() {}
    }

    class ComponentScript extends EntityScript {
        #component;
        constructor({component, ...options}={}) {
            if(typeof component!=="string" || !component)
                throw new TypeError("ComponentScript needs a component name");
            super(options);
            this.#component=component;
        }
        get componentName() { return this.#component; }
        get component() {
            return ECS.get(this.entity,this.#component);
        }
    }

    // Universal firearm gameplay in the ROOT @northstar/engine SDK.
    // Projects author weapon profiles and hit-specific gameplay; they never
    // duplicate firing cadence, YCD playback, recoil, VFX or brass spawning.
    class WeaponController extends Script {
        #profileProvider;
        #holderProvider;
        #controlsProvider;
        #aimProvider;
        #playerDriven;
        #onHit;
        #weapon=null;
        #profile=null;
        #ammo=0;
        #reserve=0;
        #cooldown=0;
        #reloadRemaining=0;
        #clipRemaining=0;
        #activeClip="";
        #attachedParent=-1;
        #attachmentMode="";
        #lastReload=false;
        #lastTrigger=false;
        #serial=0;
        #burstIndex=-1;
        #sinceShot=100;
        #status="UNEQUIPPED";
        #hudView=null;
        #hudSignature="";

        constructor({profile=null,holder=null,controls=null,aim=null,onHit=null}={}) {
            super();
            for(const [label,callback] of Object.entries({
                profile,holder,controls,aim,onHit
            })) {
                if(callback!==null && typeof callback!=="function")
                    throw new TypeError("WeaponController "+label+" must be a function");
            }
            this.#profileProvider=profile??(()=>Player.config?.weapon);
            this.#holderProvider=holder;
            this.#controlsProvider=controls;
            this.#aimProvider=aim;
            this.#playerDriven=profile===null && controls===null;
            this.#onHit=onHit;
        }

        get equipped() { return Boolean(this.#weapon?.alive); }
        get ammo() { return this.#ammo; }
        get reserveAmmo() { return this.#reserve; }
        get reloading() { return this.#reloadRemaining>0; }
        get status() { return this.#status; }
        get weaponIndex() { return this.equipped?this.#weapon.root.index:-1; }

        #cue(name,variation=0,gain=1) {
            if(!this.#profile?.audio)return 0;
            try {return Audio.play({asset:this.#profile.audio,clip:name,variation,gain});}
            catch(error) {
                Logger.warn("[engine.weapon] NAC "+name+": "+String(error));
                return 0;
            }
        }

        #animate(state,seconds=0) {
            const clips=this.#profile?.animations;
            if(!this.equipped || !clips?.dictionary || !clips[state])return false;
            const ref=clips.dictionary+"@"+clips[state];
            // Do not reset a one-frame idle clip on every update. A new
            // trigger still restarts p-fire so automatic fire stays animated.
            if(state==="idle" && this.#activeClip===ref)return true;
            try {
                Scene.playAnimation(this.weaponIndex,ref);
                this.#activeClip=ref;
                this.#clipRemaining=seconds;
                return true;
            } catch(error) {
                Logger.warn("[engine.weapon] YCD "+state+": "+String(error));
                return false;
            }
        }

        #hud() {
            const cfg=this.#profile;
            if(!cfg?.hudAsset)return;
            const signature=this.#ammo+":"+this.#reserve+":"+
                this.#status+":"+(this.reloading?"reload":"ready");
            if(signature===this.#hudSignature)return;
            this.#hudSignature=signature;
            try {
                if(!this.#hudView)
                    this.#hudView=UI.loadAsset(cfg.hudAsset,{
                        layer:cfg.hudLayer??"weapon"});
                this.#hudView.batch(()=>{
                    this.#hudView.setText("header",
                        String(cfg.displayName??"WEAPON").toUpperCase());
                    this.#hudView.setText("ammo",
                        String(this.#ammo).padStart(2,"0")+" / "+
                        String(this.#reserve).padStart(2,"0"));
                    this.#hudView.setText("state",
                        (this.reloading?"RELOADING":this.#status)+
                        (cfg.controlHint??(this.#playerDriven?
                          "   LMB FIRE  /  RMB AIM  /  R RELOAD":"")));
                });
                this.#hudView.mount();
            } catch(error) {
                Logger.warn("[engine.weapon] HUD: "+String(error));
            }
        }

        #follow() {
            if(!this.equipped || !this.#holderProvider)return;
            const holder=this.#holderProvider();
            const parent=holder?.alive?
                (holder.root?.index??holder.index??-1):-1;
            if(!Number.isSafeInteger(parent) || parent<0) {
                if(this.#attachedParent>=0) {
                    Scene.detachFromBone(this.weaponIndex);
                    this.#attachedParent=-1;
                    this.#attachmentMode="";
                }
                return;
            }
            // NPC attachments follow skeletal sockets, never the player's camera.
            const mode=this.#playerDriven &&
                Camera.mode()==="first_person"?"fps":"tps";
            if(parent===this.#attachedParent &&
               mode===this.#attachmentMode)return;
            if(this.#attachedParent>=0)
                Scene.detachFromBone(this.weaponIndex);
            const grip=this.#profile.grip;
            Scene.attachToBone(this.weaponIndex,parent,
                grip.bone,grip.offset??{},
                mode==="fps"?(grip.firstPersonRotation??grip.rotation??{}):
                    (grip.rotation??{}),
                mode==="fps"?"camera_forward":"socket");
            this.#attachedParent=parent;
            this.#attachmentMode=mode;
        }

        #recoil() {
            const r=this.#profile?.recoil??{};
            // Camera kick should not throw the iron sights off the target
            // as violently as an unaimed shot. All coefficients remain
            // weapon-authored; neither NPCs nor the renderer hardcode them.
            const ads=this.#playerDriven &&
                Camera.mode()==="first_person" && Input.mouseDown(1)?
                (r.aimDownSights??{}):{};
            // Deterministic multi-stage pattern, not per-shot random jitter.
            const pattern=r.shotPattern;
            const stage=Array.isArray(pattern)&&pattern.length?
                pattern[this.#burstIndex%pattern.length]:null;
            const pitchScale=stage?.pitchScale??1;
            const yawScale=stage?.yawScale??0;
            if(![pitchScale,yawScale].every(Number.isFinite) ||
               pitchScale<0 || pitchScale>3 || Math.abs(yawScale)>3)
                throw new RangeError("Invalid authored recoil pattern");
            const yawBase=ads.yawRadians??r.yawRadians??0;
            return {
                amplitude:ads.amplitude??r.amplitude??0.010,
                duration:ads.duration??r.duration??0.10,
                pitchRadians:(ads.pitchRadians??r.pitchRadians??0.045)*pitchScale,
                yawRadians:yawBase + yawScale*(r.patternYawRadians??0),
                recoverySeconds:ads.recoverySeconds??r.recoverySeconds??0.25,
                weaponBackMeters:ads.weaponBackMeters??r.weaponBackMeters??0,
                armImpulse:ads.armImpulse??r.armImpulse??0,
                snapSeconds:ads.snapSeconds??r.snapSeconds??.045,
                accentSeconds:ads.accentSeconds??r.accentSeconds??.035,
                settleSeconds:ads.settleSeconds??r.settleSeconds??.14,
                aftershock:ads.aftershock??r.aftershock??.18
            };
        }

        #fire() {
            if(this.#ammo<=0) {
                this.#status="EMPTY";
                this.#cooldown=0.24;
                this.#cue("empty");
                this.#animate("empty",0.167);
                this.#hud();
                return;
            }
            // Mechanical firing is authoritative even when an optional
            // attachment snapshot/VFX provider has not become ready.
            --this.#ammo;
            ++this.#serial;
            this.#burstIndex=this.#sinceShot>.36?0:this.#burstIndex+1;
            this.#sinceShot=0;
            this.#cooldown=1/this.#profile.roundsPerSecond;
            this.#status="FIRED";
            this.#animate("fire",this.#profile.fireAnimationSeconds??0.17);
            this.#cue("fire_body",0,this.#profile.fireGain??1);
            let shot=null;
            const holder=this.#holderProvider?.();
            // The possessed Pawn owns the Jolt character capsule; the
            // attached skeletal visual is a different scene entity. Ignoring
            // only that visual makes the camera ray strike our own capsule.
            const ownerIndex=this.#playerDriven && Player.index>=0?
                Player.index:(holder?.alive?
                    (holder.root?.index??holder.index??-1):-1);
            const aim=this.#aimProvider?.()??{};
            try {
                shot=WeaponBallistics.solve({
                    weaponIndex:this.weaponIndex,
                    muzzleLocal:this.#profile.muzzleLocal,
                    ejectionLocal:this.#profile.ejectionLocal,
                    barrelAxisLocal:this.#profile.barrelAxisLocal,
                    range:this.#profile.range,
                    radius:this.#profile.sweepRadius,
                    adsRadius:this.#profile.aimSweepRadius??0.006,
                    eye:aim.eye??null,
                    aimDirection:aim.direction??null,
                    mode:aim.mode??(this.#playerDriven?"auto":"barrel"),
                    ignoreIndex:ownerIndex
                });
            } catch(error) {
                Logger.warn("[engine.weapon] Ballistics: "+String(error));
            }
            // Only the locally owned camera receives kick by default.
            // World NPC weapons still emit VFX, damage and physical casings.
            const cameraRecoil=this.#profile.cameraRecoil??this.#playerDriven;
            if(shot) {
                try {
                    WeaponEffects.fire({
                        shot,
                        casingType:this.#profile.casingType??"pistol",
                        casingPrefab:this.#profile.casingPrefab??null,
                        shellImpulse:this.#profile.shellImpulse??1,
                        casingAudio:this.#profile.casingAudio??
                            this.#profile.audio??null,
                        casingMaterial:this.#profile.casingMaterial??
                            {friction:.30,restitution:.42},
                        emitCasing:this.#profile.emitCasing??true,
                        recoil:cameraRecoil?this.#recoil():null
                    });
                } catch(error) {
                    Logger.warn("[engine.weapon] Effects: "+String(error));
                    if(cameraRecoil)WeaponEffects.kick(this.#recoil());
                }
            } else {
                if(cameraRecoil)WeaponEffects.kick(this.#recoil());
                // No eye-origin VFX/ballistics fallback: the authored
                // physical barrel socket must become available.
                if(this.#serial<=3 || this.#serial%20===0)
                    Logger.warn("[engine.weapon] Missing weapon socket snapshot; "+
                        "shot has recoil/animation but no projectile or muzzle VFX");
            }
            if(shot?.hit && !shot.blocked && shot.hit.index!==ownerIndex) {
                const target=shot.hit.index>=0?Scene.get(shot.hit.index):null;
                if(target && Number(target.mass)>0 &&
                   Number.isFinite(this.#profile.impactImpulse)) {
                    const direction=shot.direction;
                    Physics.applyImpulse(shot.hit.index,{
                        x:direction.x*this.#profile.impactImpulse,
                        y:direction.y*this.#profile.impactImpulse,
                        z:direction.z*this.#profile.impactImpulse
                    });
                }
                this.#status="HIT";
                this.#onHit?.({hit:shot.hit,shot,target,weapon:this});
            }
            this.#hud();
        }

        /** Explicit fire for AI, cinematic sequences and scripted turrets. */
        fire() {
            if(!this.equipped || this.reloading || this.#cooldown>0)return false;
            this.#fire();
            return true;
        }

        reload() {
            if(!this.equipped || this.reloading ||
               this.#ammo>=this.#profile.magazineSize ||
               this.#reserve<=0)return false;
            this.#reloadRemaining=this.#profile.reloadSeconds;
            this.#status="RELOADING";
            this.#cue("reload");
            this.#animate("reload",this.#reloadRemaining);
            this.#hud();
            return true;
        }

        start() {
            const cfg=this.#profileProvider();
            if(!cfg?.enabled)return;
            // Legacy player-owned controllers still require possession.
            // Explicit profiles/holders may belong to an NPC or a free object.
            if(this.#playerDriven && !this.#holderProvider &&
               (Player.index<0 || !Player.entityId))
                throw new Error("Player weapon requires a possessed Pawn");
            for(const key of ["magazineSize","roundsPerSecond",
                               "reloadSeconds","range","sweepRadius"]) {
                if(!Number.isFinite(cfg[key]) || cfg[key]<=0)
                    throw new RangeError("Invalid weapon profile: "+key);
            }
            if(!Number.isSafeInteger(cfg.magazineSize) ||
               !Number.isSafeInteger(cfg.reserveAmmo) || cfg.reserveAmmo<0 ||
               !["semi","auto"].includes(cfg.fireMode??"semi"))
                throw new RangeError("Weapon magazine, reserve or fireMode invalid");
            if(typeof cfg.prefab!=="string" || !cfg.prefab ||
               (this.#holderProvider && !cfg.grip?.bone) ||
               !cfg.muzzleLocal ||
               (cfg.emitCasing!==false && !cfg.ejectionLocal))
                throw new TypeError("Weapon profile requires prefab, muzzleLocal, "+
                    "grip for attached weapons and ejectionLocal when casing is enabled");
            this.#profile=cfg;
            this.#ammo=cfg.magazineSize;
            this.#reserve=cfg.reserveAmmo;
            this.#cooldown=this.#reloadRemaining=this.#clipRemaining=0;
            this.#activeClip=this.#hudSignature="";
            this.#serial=0;
            this.#burstIndex=-1;
            this.#sinceShot=100;
            this.#lastReload=this.#lastTrigger=false;
            this.#attachedParent=-1;
            this.#attachmentMode="";
            const holder=this.#holderProvider?.();
            const holderIndex=holder?.alive?
                (holder.root?.index??holder.index??-1):-1;
            const position=cfg.position??holder?.root?.snapshot?.position??
                holder?.snapshot?.position??
                (holderIndex>=0?Scene.get(holderIndex)?.position:null)??
                (this.#playerDriven && Player.index>=0?Player.eye():null);
            if(!position)throw new Error(
                "World weapon requires profile.position or a living holder");
            Contracts.vector(position,"Weapon spawn position");
            // Equipped visual is socket-driven: physical world prefab keeps real mass.
            this.#weapon=Factory.prefab(cfg.prefab,{
                position,physics:"visual"});
            this.#follow();
            this.#animate("idle");
            this.#cue("equip");
            this.#status="READY";
            this.#hud();
            Logger.info("[engine.weapon] Equipped "+
                String(cfg.displayName??cfg.prefab));
        }

        update(dt) {
            if(!this.equipped || Player.paused ||
               !Number.isFinite(dt) || dt<0 || dt>0.5)return;
            this.#follow();
            this.#sinceShot=Math.min(100,this.#sinceShot+dt);
            // Advance only once from the weapon controller. The project
            // visual samples this channel but does not advance it.
            WeaponEffects.advanceMotion?.(dt);
            // Contact dispatch is owned by Physics, once per engine tick.
            if(this.#clipRemaining>0 && !this.reloading) {
                this.#clipRemaining=Math.max(0,this.#clipRemaining-dt);
                if(this.#clipRemaining===0)this.#animate("idle");
            }
            this.#cooldown=Math.max(0,this.#cooldown-dt);
            const wasReloading=this.reloading;
            this.#reloadRemaining=Math.max(0,this.#reloadRemaining-dt);
            if(wasReloading && !this.reloading) {
                const loaded=Math.min(
                    this.#profile.magazineSize-this.#ammo,this.#reserve);
                this.#ammo+=loaded;
                this.#reserve-=loaded;
                this.#status="READY";
                this.#animate("idle");
                this.#hud();
            }
            const controls=this.#controlsProvider?
                Contracts.options(this.#controlsProvider(),"Weapon controls"):
                (this.#playerDriven?
                 {reload:Input.isDown(NorthStar.modules.engine.Keys.R),
                  trigger:Input.mouseDown(0)}:
                 {reload:false,trigger:false});
            if(typeof controls.reload!=="boolean" ||
               typeof controls.trigger!=="boolean")
                throw new TypeError("Weapon controls require boolean trigger/reload");
            const reloadDown=controls.reload;
            if(reloadDown && !this.#lastReload)this.reload();
            this.#lastReload=reloadDown;
            const trigger=controls.trigger;
            if(trigger && !this.#lastTrigger && !this.#ammo &&
               this.#reserve>0)this.reload();
            const mode=this.#profile.fireMode??"semi";
            if(trigger && !this.reloading && this.#cooldown<=0 &&
               (mode==="auto" || !this.#lastTrigger))this.fire();
            this.#lastTrigger=trigger;
        }

        stop() {
            if(this.#weapon) {
                if(this.#attachedParent>=0 && this.#weapon.alive)
                    Scene.detachFromBone(this.#weapon.root.index);
                if(this.#weapon.alive)this.#weapon.destroy();
                this.#weapon=null;
            }
            this.#hudView?.dispose();
            this.#hudView=null;
            this.#hudSignature="";
            this.#attachedParent=-1;
            this.#attachmentMode="";
            this.#profile=null;
            this.#burstIndex=-1;
            this.#sinceShot=100;
            WeaponEffects.resetMotion?.();
            this.#status="UNEQUIPPED";
        }
    }
    // Shared vehicle controls; project rig configuration remains in .nvehicle.
    class VehicleHandle {
        #index;
        constructor(index) {
            this.#index = Contracts.index(index, "Vehicle index");
        }
        get index() { return this.#index; }
        get state() { return Vehicle.state(this.#index); }
        get motion() { return Vehicle.motion(this.#index); }
        get powertrain() { return Vehicle.powertrainState(this.#index); }
        drive(controls) { return Vehicle.drive(this.#index, controls); }
        navigate(destination) { return Vehicle.navigate(this.#index, destination); }
        selectGear(gear) { return Vehicle.selectGear(this.#index, gear); }
        stop() { return Vehicle.drive(this.#index, {brake: 1}); }
        detach() { return Vehicle.detach(this.#index); }
    }
    class Vehicle {
        static #index(value) { return Contracts.index(value, "Vehicle index"); }
        static #number(value, name, min, max) {
            return Contracts.number(value,name,min,max);
        }
        static available() { return typeof engine.vehicle.drive?.controls === "function"; }
        /** Non-owning handle; never spawns a duplicate vehicle. */
        static handle(index) { return new VehicleHandle(Vehicle.#index(index)); }
        static attach(index, wheelbase, maxSpeed) {
            Vehicle.#index(index);
            Vehicle.#number(wheelbase, "wheelbase", 0.1, 100);
            Vehicle.#number(maxSpeed, "maxSpeed", 0.1, 1000);
            return engine.vehicle.core.attach(index, wheelbase, maxSpeed);
        }
        static configureRig(index, path) {
            Vehicle.#index(index);
            return engine.vehicle.drive.configureRig(index,
                Contracts.vfs(path, ".nvehicle"));
        }
        static controls(index, throttle, steer, brake) {
            Vehicle.#index(index);
            Vehicle.#number(throttle, "throttle", -1, 1);
            Vehicle.#number(steer, "steer", -1, 1);
            Vehicle.#number(brake, "brake", 0, 1);
            return engine.vehicle.drive.controls(index, throttle, steer, brake);
        }
        static drive(index, options = {}) {
            const {throttle = 0, steer = 0, brake = 0} =
                Contracts.options(options,"Vehicle controls");
            return Vehicle.controls(index, throttle, steer, brake);
        }
        static waypoint(index, x, z, speed, radius = 2) {
            Vehicle.#index(index);
            Vehicle.#number(x, "waypoint.x", -1e6, 1e6);
            Vehicle.#number(z, "waypoint.z", -1e6, 1e6);
            Vehicle.#number(speed, "waypoint.speed", 0, 1000);
            Vehicle.#number(radius, "waypoint.radius", 0.1, 1000);
            return engine.vehicle.navigation.waypoint(index, x, z, speed, radius);
        }
        static navigate(index, options = {}) {
            const {x, z, speed, radius = 2} =
                Contracts.options(options,"Vehicle destination");
            return Vehicle.waypoint(index, x, z, speed, radius);
        }
        static state(index) { return engine.vehicle.core.state(Vehicle.#index(index)); }
        static motion(index) { return engine.vehicle.core.motion(Vehicle.#index(index)); }
        static powertrainState(index) { return engine.vehicle.drive.powertrainState(Vehicle.#index(index)); }
        static selectGear(index, gear) {
            Vehicle.#index(index);
            if (!Number.isSafeInteger(gear) || gear < -1 || gear > 8)
                throw new RangeError("Gear must be -1, 0 or 1..8");
            return engine.vehicle.drive.selectGear(index, gear);
        }
        static wheelState(index, wheel) { return engine.vehicle.wheels.wheelState(Vehicle.#index(index), wheel); }
        static wheelPose(index, wheel) { return engine.vehicle.wheels.wheelPose(Vehicle.#index(index), wheel); }
        static wheelCount(index) { return engine.vehicle.wheels.wheelCount(Vehicle.#index(index)); }
        static seatCount(index) { return engine.vehicle.seats.seatCount(Vehicle.#index(index)); }
        static seatState(index, seat) { return engine.vehicle.seats.seatState(Vehicle.#index(index), seat); }
        static occupy(index, seat, entity) { return engine.vehicle.seats.occupy(Vehicle.#index(index), seat, entity); }
        static release(index, seat, entity) { return engine.vehicle.seats.release(Vehicle.#index(index), seat, entity); }
        static setDoor(index, door, open) { return engine.vehicle.doors.setDoor(Vehicle.#index(index), door, open); }
        static doorCount(index) { return engine.vehicle.doors.doorCount(Vehicle.#index(index)); }
        static doorState(index, door) { return engine.vehicle.doors.doorState(Vehicle.#index(index), door); }
        static detach(index) { return engine.vehicle.core.detach(Vehicle.#index(index)); }
        static provider() { return engine.vehicle.provider.provider(); }
        static count() { return engine.vehicle.core.count(); }
    }

    // One factory for engine-owned services. Projects author descriptors,
    // not new scene, vehicle, weapon, physics or camera implementations.
    // Registration is an object with an explicit lifetime, not an anonymous
    // bookkeeping function exposed as an improvised service.
    class FactoryRegistration {
        #release;
        constructor(release) {
            this.#release = release;
            Object.freeze(this);
        }
        dispose() { return this.#release(); }
    }

    class Factory {
        static #custom = new Map();
        // Canonical built-in recipes: one registry powers discovery and dispatch.
        // Native systems still own object lifetime and provider implementation.
        static #builtins = Object.freeze({
            entity: descriptor => Factory.entity(descriptor),
            prefab: descriptor => {
                if (typeof descriptor === "string") return Factory.prefab(descriptor);
                Contracts.options(descriptor, "Prefab request");
                return Factory.prefab(descriptor.path, descriptor.options ?? {});
            },
            existing: descriptor => {
                if (typeof descriptor === "string") return Factory.existing(descriptor);
                Contracts.options(descriptor, "Existing entity request");
                return Factory.existing(descriptor.name, descriptor.required ?? true);
            },
            vehicle: descriptor => Factory.vehicle(descriptor),
            weapon: descriptor => Factory.weapon(descriptor ?? {}),
            camera: descriptor => Factory.camera(descriptor ?? {}),
            character: descriptor => Factory.character(descriptor ?? {}),
            script: descriptor => Factory.script(
                descriptor.Type,descriptor.options??{}),
            entityScript: descriptor => Factory.entityScript(
                descriptor.Type,descriptor.entity,descriptor.options??{})
        });

        static entity(descriptor) {
            return Scene.spawn(Contracts.options(descriptor, "Entity descriptor"));
        }
        static prefab(path, options = {}) {
            return NorthStar.sceneTypes.Prefab.instantiate(
                Contracts.vfs(path, ".nprefab", "prefabs/"),
                Contracts.options(options, "Prefab options"));
        }
        /** Non-owning handle to an existing authored actor. No duplicates. */
        static existing(name, required = true) {
            if (typeof name !== "string" || !name.trim())
                throw new TypeError("Existing entity requires a nonempty name");
            if (typeof required !== "boolean")
                throw new TypeError("required must be boolean");
            const handle = Scene.handleByName(name);
            if (!handle && required) throw new Error("Required entity not found: " + name);
            return handle;
        }
        static vehicle(index) {
            return Vehicle.handle(Contracts.index(index, "Vehicle index"));
        }
        static weapon(options = {}) {
            return new WeaponController(Contracts.options(options, "Weapon options"));
        }
        static camera(options = {}) {
            return Camera.createViewController(Contracts.options(options, "Camera options"));
        }
        static character(options = {}) {
            return Character.createController(Contracts.options(options, "Character options"));
        }

        static script(Type, options = {}) {
            if(typeof Type!=="function" || !(Type.prototype instanceof Script))
                throw new TypeError("Factory.script requires a Script subclass");
            return new Type(Contracts.options(options,"Script options")).install();
        }
        static entityScript(Type, entity, options = {}) {
            if(typeof Type!=="function" ||
                !(Type.prototype instanceof EntityScript))
                throw new TypeError("Factory.entityScript requires EntityScript subclass");
            if(!entity || entity.alive!==true)
                throw new Error("Factory.entityScript requires a living entity handle");
            return new Type({...Contracts.options(options,"EntityScript options"),entity})
                .install();
        }

        /** Extensible project-scoped factories, isolated from core names. */
        static register(kind, make) {
            if (typeof kind !== "string" ||
                !/^project\.[a-zA-Z][a-zA-Z0-9_.-]{0,63}$/.test(kind) ||
                typeof make !== "function")
                throw new TypeError("Custom factory requires project.* name and callback");
            if (Factory.#custom.has(kind))
                throw new Error("Factory recipe already registered: " + kind);
            Factory.#custom.set(kind, make);
            // The original disposer must never remove a subsequent registration.
            return new FactoryRegistration(() => {
                if (Factory.#custom.get(kind) !== make) return false;
                return Factory.#custom.delete(kind);
            });
        }
        static kinds() {
            return Object.freeze([...Object.keys(Factory.#builtins),
                ...Factory.#custom.keys()]);
        }
        /** Generic entrypoint useful for declarative tools and project scripts. */
        static create(kind, descriptor) {
            if (Object.hasOwn(Factory.#builtins, kind))
                return Factory.#builtins[kind](descriptor);
            const custom = typeof kind === "string" ? Factory.#custom.get(kind) : undefined;
            if (!custom)
                throw new RangeError("Unknown factory recipe: " + String(kind));
            return custom(descriptor, API);
        }
    }

    // Single discoverable entrypoint, not an alternate host or duplicated
    // simulation. Stable service references delegate to the native providers.
    // A concrete, immutable OOP instance is the only public service root.
    // Its getters return the canonical classes; no second implementations
    // or ad-hoc plain-object "API service" can drift from the actual engine.
    class EngineAPI {
        static #constructed = false;
        constructor() {
            if (EngineAPI.#constructed)
                throw new Error("Use the EngineAPI singleton exported as API");
            EngineAPI.#constructed = true;
            Object.freeze(this);
        }
        get Factory() { return Factory; }
        get entity() { return Scene; }
        get prefab() { return NorthStar.sceneTypes.Prefab; }
        get vehicle() { return Vehicle; }
        get weapon() { return WeaponController; }
        get ballistics() { return WeaponBallistics; }
        get weaponEffects() { return WeaponEffects; }
        get camera() { return Camera; }
        get physics() { return Physics; }
        get collision() { return Collision; }
        get particles() { return Particles; }
        get character() { return Character; }
        get player() { return Player; }
        get events() { return Events; }
        get input() { return Input; }
        get audio() { return Audio; }
        get audioVoice() { return AudioVoice; }
        get ui() { return UI; }
        get session() { return Session; }
        get logger() { return Logger; }
        get ecs() { return ECS; }
        get time() { return Time; }
        get scheduler() { return Scheduler; }
        get assets() { return Assets; }
        get scripts() { return Script; }
        get capabilities() { return Capabilities; }
        get CapabilityError() { return EngineCapabilityError; }
        get storage() { return Storage; }
        get render() { return Render; }
    }
    const API = new EngineAPI();

    // One resource facade: delegate to authoritative scene/UI asset paths.
    // Generic asset streaming requires the native engine.assets capability.
    class Assets {
        static available(kind) {
            if(kind==="prefab"||kind==="ui")return true;
            return typeof engine.assets?.files?.open==="function";
        }
        static prefab(path,options={}) {
            return Factory.prefab(path,options);
        }
        static ui(path,options={}) {
            return UI.loadAsset(Contracts.vfs(path,".nui","ui/"),
                Contracts.options(options,"UI asset options"));
        }
        static open(path,kind,options={}) {
            Contracts.vfs(path,"","");
            if(typeof kind!=="string" || !kind)
                throw new TypeError("Asset kind is required");
            if(kind==="prefab")return Assets.prefab(path,options);
            if(kind==="ui")return Assets.ui(path,options);
            if(!Assets.available(kind))
                throw new EngineCapabilityError("assets.open:"+kind);
            return engine.assets.files.open(kind,path,
                Contracts.options(options,"Asset load options"));
        }
    }

    // Project-agnostic persistence. Gameplay decides what each key means.
    class Storage {
        static #key(key) {
            if (typeof key!=="string" ||
                !/^[a-zA-Z0-9_./-]{1,96}$/.test(key) ||
                key.includes(".."))
                throw new TypeError("Invalid project storage key");
            return key;
        }
        static read(key) {
            if (typeof engine.storage.saves?.read!=="function")
                throw new EngineCapabilityError("save.read");
            return engine.storage.saves.read(Storage.#key(key));
        }
        static write(key,value) {
            if (typeof value!=="string" || value.length>262144)
                throw new TypeError("Storage.write needs bounded serialized data");
            if (typeof engine.storage.saves?.write!=="function")
                throw new EngineCapabilityError("save.write");
            return engine.storage.saves.write(Storage.#key(key),value);
        }
    }

    // Native rendering capability facade, not a second renderer.
    class RenderProvider {
        static name() {
            return engine.render.provider.name();
        }
        static providerVersion() {
            return engine.render.provider.version();
        }
        static version() {
            if (typeof engine.render.provider?.engineVersion!=="function")
                throw new EngineCapabilityError("render.provider.engineVersion");
            return engine.render.provider.engineVersion();
        }
        static setDetailLevel(level) {
            if (!["balanced","high","ultra","cinematic"].includes(level))
                throw new RangeError("Unknown rendering quality preset");
            if (typeof engine.render.provider?.setDetailLevel!=="function")
                throw new EngineCapabilityError("render.provider.setDetailLevel");
            return engine.render.provider.setDetailLevel(level);
        }
        static detailLevel() {
            if (typeof engine.render.provider?.detailLevel!=="function")
                throw new EngineCapabilityError("render.provider.detailLevel");
            return engine.render.provider.detailLevel();
        }
    }

    // Engine Render is the root gateway for built-in and project shader assets.
    // Source authority is native engine.render; no separate JS compiler or
    // renderer-specific duplicate shader registry is created.
    class RenderShaders {
        static #native(method) {
            const nested={shaderCatalog:"catalog",shaderSource:"source",
                          shaderQueueCompiled:"queueCompiled",shaderPoll:"poll"};
            const fn=engine.render?.shaders?.[nested[method]];
            if(typeof fn!=="function")
                throw new EngineCapabilityError("engine.render.shaders."+nested[method]);
            return fn;
        }
        static #path(value,label="Shader path") {
            if(typeof value!=="string" ||
               value.length<12 || value.length>256 ||
               !value.startsWith("shaders/") ||
               !/^[a-zA-Z0-9_./-]+$/.test(value) ||
               value.includes("..") || value.includes("//") ||
               !/\.(glsl|hlsl|vert|frag|comp|geom|tesc|tese)$/.test(value))
                throw new TypeError(label+" must be a safe shaders/* source path");
            return value;
        }
        static #binary(value) {
            if(typeof value!=="string" || value.length>256 ||
               !value.startsWith("shaders/") ||
               !/^[a-zA-Z0-9_./-]+$/.test(value) ||
               value.includes("..") || value.includes("//") ||
               !/\.(spv|dxil)$/.test(value))
                throw new TypeError("Compiled shader must be shaders/*.spv or shaders/*.dxil");
            return value;
        }
        static catalog() {
            const catalog=RenderShaders.#native("shaderCatalog")();
            if(!catalog || !Array.isArray(catalog.builtin))
                throw new Error("engine.render returned malformed shader catalog");
            const builtin=Object.freeze(catalog.builtin.map(record=>
                Object.freeze({...record})));
            return Object.freeze({...catalog,builtin});
        }
        static source(path) {
            return RenderShaders.#native("shaderSource")(RenderShaders.#path(path));
        }
        static load(path) {
            const name=RenderShaders.#path(path);
            const source=RenderShaders.source(name);
            const language=name.endsWith(".hlsl")?"hlsl":"glsl";
            return Object.freeze({path:name,language,source});
        }
        static queueCompiled(sourcePath,binaryPath) {
            const source=RenderShaders.#path(sourcePath);
            const binary=RenderShaders.#binary(binaryPath);
            if((source.endsWith(".hlsl") && !binary.endsWith(".dxil")) ||
               (!source.endsWith(".hlsl") && !binary.endsWith(".spv")))
                throw new TypeError("Compiled shader format does not match source language");
            // Native hot reload queues an existing compiled binary for the
            // matching active material. It does not compile source code.
            return RenderShaders.#native("shaderQueueCompiled")(source,binary);
        }
        static poll() {
            const status=RenderShaders.#native("shaderPoll")();
            if(!status || typeof status.pending!=="boolean" ||
               !Array.isArray(status.completed))
                throw new Error("engine.render returned malformed shader status");
            return Object.freeze({
                pending:status.pending,
                completed:Object.freeze([...status.completed])
            });
        }
        static get supportsHotReload() {
            return RenderShaders.catalog().hotReload;
        }
    }

    // Canonical render API: grouped, provider-neutral services only.
    class Render {
        static get shaders(){return RenderShaders;}
        static get shadows(){return RenderShadows;}
        // A rendering facade for the one world-owned engine.render.sky state.
        static get sky(){return Sky;}
        static get provider(){return RenderProvider;}
        static version(){return RenderProvider.version();}
        static setDetailLevel(level){return RenderProvider.setDetailLevel(level);}
        static detailLevel(){return RenderProvider.detailLevel();}
    }
    // Nested render/shadow facade. The native provider owns all GPU resources;
    // this immutable SDK layer never invents a second shadow implementation.
    class RenderShadows {
        static #native(name) {
            const fn=engine.render?.shadows?.[name];
            if(typeof fn!=="function")
                throw new EngineCapabilityError("engine.render.shadows."+name);
            return fn;
        }
        static state() {
            const value=RenderShadows.#native("state")();
            if(!value || typeof value!=="object" ||
               typeof value.requested!=="boolean" ||
               typeof value.verifiedOnGpu!=="boolean")
                throw new Error("Malformed native shadow configuration");
            return Object.freeze({...value});
        }
        static capabilities() {
            const value=RenderShadows.#native("capabilities")();
            if(!value || typeof value!=="object" ||
               typeof value.csm!=="boolean" ||
               typeof value.pcf!=="boolean" ||
               typeof value.pcss!=="boolean")
                throw new Error("Malformed native shadow capabilities");
            return Object.freeze({...value});
        }
    }

    // Render Sky facade. The world owns sky state; the renderer consumes its snapshot.
    // State and simulation live in the native engine.render.sky provider; all GPU
    // backends consume one snapshot. Never create a duplicate JS clock.
    class Sky {
        static #native(method) {
            const fn=engine.render.sky?.[method];
            if(typeof fn!=="function")
                throw new EngineCapabilityError("engine.render.sky."+method);
            return fn;
        }
        static configure(settings={}) {
            if(!settings || typeof settings!=="object" || Array.isArray(settings))
                throw new TypeError("Sky.configure requires a settings object");
            const booleans=new Set(["enabled","paused","sun","moon","stars","clouds"]);
            const ranges={
                timeOfDay:[0,24],
                dayLengthSeconds:[30,86400],
                latitudeDegrees:[-90,90],
                dayOfYear:[1,365],
                moonPhase:[0,1],
                cloudCoverage:[0,1],
                cloudSpeed:[0,1],
                haze:[0,1],
                fogDensity:[0,.25],
                sunIntensity:[0,8],
                moonIntensity:[0,4],
                starDensity:[0,1]
            };
            for(const [name,value] of Object.entries(settings)) {
                if(booleans.has(name)) {
                    if(typeof value!=="boolean")
                        throw new TypeError("Sky."+name+" must be boolean");
                    continue;
                }
                if(name==="mode") {
                    if(!["procedural","drawable","hybrid","disabled"].includes(value))
                        throw new RangeError("Invalid Sky mode");
                    continue;
                }
                if(name==="drawable" || name==="preset") {
                    if(typeof value!=="string" || value.length>512)
                        throw new TypeError("Sky."+name+" must be a string");
                    continue;
                }
                const range=ranges[name];
                if(!range)throw new RangeError("Unknown Sky property: "+name);
                if(typeof value!=="number" || !Number.isFinite(value) ||
                   value<range[0] || value>range[1] ||
                   (name==="timeOfDay" && value===24) ||
                   (name==="dayOfYear" && !Number.isInteger(value)))
                    throw new RangeError("Sky."+name+" out of range");
            }
            Sky.#native("configure")(JSON.stringify(settings));
            return Sky.state();
        }
        static state() {
            const source=Sky.#native("state")();
            if(!source || typeof source!=="object" ||
               !source.sunDirection || !source.moonDirection)
                throw new Error("engine.render.sky returned invalid state");
            return Object.freeze({
                ...source,
                sunDirection:Object.freeze({...source.sunDirection}),
                moonDirection:Object.freeze({...source.moonDirection})
            });
        }
        static enable(enabled=true) {
            if(typeof enabled!=="boolean")
                throw new TypeError("Sky.enable expects boolean");
            return Sky.configure({enabled});
        }
        static pause(paused=true) {
            if(typeof paused!=="boolean")
                throw new TypeError("Sky.pause expects boolean");
            return Sky.configure({paused});
        }
        static setTimeOfDay(hours,minutes=0) {
            if(!Number.isInteger(hours)||hours<0||hours>23 ||
               !Number.isInteger(minutes)||minutes<0||minutes>59)
                throw new RangeError("Sky time must be 00:00..23:59");
            // Explicitly opting into celestial time also enables the sky.
            return Sky.configure({enabled:true,timeOfDay:hours+minutes/60});
        }
        static setDayLength(seconds) {
            return Sky.configure({enabled:true,dayLengthSeconds:seconds});
        }
        static setCloudCoverage(coverage) {
            return Sky.configure({enabled:true,clouds:true,cloudCoverage:coverage});
        }
        static setClouds(settings={}) {
            if(typeof settings==="number")
                settings={coverage:settings};
            if(!settings || typeof settings!=="object"||Array.isArray(settings))
                throw new TypeError("Sky.setClouds expects coverage or object");
            const accepted=new Set(["enabled","coverage","speed"]);
            for(const key of Object.keys(settings))
                if(!accepted.has(key))
                    throw new RangeError("Unknown Sky cloud property: "+key);
            const next={enabled:true};
            if(settings.enabled!==undefined)next.clouds=settings.enabled;
            else next.clouds=true;
            if(settings.coverage!==undefined)
                next.cloudCoverage=settings.coverage;
            if(settings.speed!==undefined)
                next.cloudSpeed=settings.speed;
            return Sky.configure(next);
        }
        static setSun(settings={}) {
            if(typeof settings==="boolean")settings={enabled:settings};
            if(!settings || typeof settings!=="object"||Array.isArray(settings))
                throw new TypeError("Sky.setSun expects boolean or object");
            for(const key of Object.keys(settings))
                if(key!=="enabled"&&key!=="intensity")
                    throw new RangeError("Unknown Sky sun property: "+key);
            const next={enabled:true,sun:settings.enabled??true};
            if(settings.intensity!==undefined)next.sunIntensity=settings.intensity;
            return Sky.configure(next);
        }
        static setMoon(settings={}) {
            if(typeof settings==="boolean")settings={enabled:settings};
            if(!settings||typeof settings!=="object"||Array.isArray(settings))
                throw new TypeError("Sky.setMoon expects boolean or object");
            for(const key of Object.keys(settings))
                if(!["enabled","intensity","phase"].includes(key))
                    throw new RangeError("Unknown Sky moon property: "+key);
            const next={enabled:true,moon:settings.enabled??true};
            if(settings.intensity!==undefined)next.moonIntensity=settings.intensity;
            if(settings.phase!==undefined)next.moonPhase=settings.phase;
            return Sky.configure(next);
        }
        static setStars(settings={}) {
            if(typeof settings==="boolean")settings={enabled:settings};
            if(!settings||typeof settings!=="object"||Array.isArray(settings))
                throw new TypeError("Sky.setStars expects boolean or object");
            for(const key of Object.keys(settings))
                if(key!=="enabled"&&key!=="density")
                    throw new RangeError("Unknown Sky stars property: "+key);
            const next={enabled:true,stars:settings.enabled??true};
            if(settings.density!==undefined)next.starDensity=settings.density;
            return Sky.configure(next);
        }
        // Explicit deterministic stepping is intended for scripted events or
        // tests; ordinary frame time is advanced by the engine.
        static advance(seconds) {
            if(typeof seconds!=="number" || !Number.isFinite(seconds) ||
               seconds<0||seconds>1)
                throw new RangeError("Sky.advance must be 0..1 seconds");
            Sky.#native("advance")(seconds);
            return Sky.state();
        }
        static reset() {
            Sky.#native("reset")();
            return Sky.state();
        }
        static get timeOfDay(){return Sky.state().timeOfDay;}
        static get sunDirection(){return Sky.state().sunDirection;}
        static get moonDirection(){return Sky.state().moonDirection;}
        static get daylight(){return Sky.state().daylight;}
    }
    // Public module registry is also an object, not a mutable ad-hoc record.
    // Each getter resolves the canonical class from this V8 context.
    class EngineModuleRegistry {
        #inputTypes;
        constructor(inputTypes) {
            this.#inputTypes = inputTypes;
            Object.freeze(this);
        }
        get API() { return API; }
        get EngineAPI() { return EngineAPI; }
        get Factory() { return Factory; }
        get Camera() { return Camera; }
        get Input() { return Input; }
        get Events() { return Events; }
        get Audio() { return Audio; }
        get AudioVoice() { return AudioVoice; }
        get Character() { return Character; }
        get Player() { return Player; }
        get Vehicle() { return Vehicle; }
        get Script() { return Script; }
        get PlayerScript() { return PlayerScript; }
        get InputAction() { return InputAction; }
        get Scene() { return Scene; }
        get Physics() { return Physics; }
        get Collision() { return Collision; }
        get Particles() { return Particles; }
        get WeaponBallistics() { return WeaponBallistics; }
        get WeaponEffects() { return WeaponEffects; }
        get WeaponController() { return WeaponController; }
        get Session() { return Session; }
        get UI() { return UI; }
        get Logger() { return Logger; }
        get AuthoredEntityHandle() { return AuthoredEntityHandle; }
        get EntityScript() { return EntityScript; }
        get ComponentScript() { return ComponentScript; }
        get ScriptContext() { return ScriptContext; }
        get Time() { return Time; }
        get Scheduler() { return Scheduler; }
        get ScheduledTask() { return ScheduledTask; }
        get ECS() { return ECS; }
        get Assets() { return Assets; }
        get Capabilities() { return Capabilities; }
        get EngineCapabilityError() { return EngineCapabilityError; }
        get Storage() { return Storage; }
        get Render() { return Render; }
        get RenderProvider() { return RenderProvider; }
        get RenderSky() { return Sky; }
        get RenderShaders() { return RenderShaders; }
        get RenderShadows() { return RenderShadows; }
        get Keys() { return this.#inputTypes.Keys; }
        get GamepadButtons() { return this.#inputTypes.GamepadButtons; }
        get Entity() { return globalThis.NorthStar.sceneTypes.Entity; }
        get Prefab() { return globalThis.NorthStar.sceneTypes.Prefab; }
    }

    class SDKModuleCatalog {
        #engine;
        constructor(engine) {
            this.#engine = engine;
            Object.freeze(this);
        }
        get engine() { return this.#engine; }
    }

    class NorthStarSDK {
        constructor() {
            const inputTypes = new InputTypes();
            const engineModules = new EngineModuleRegistry(inputTypes);
            Object.defineProperties(this, {
                apiVersion: {value: 1, enumerable: true},
                camera: {value: new CameraService(), enumerable: true},
                character: {value: new CharacterService(), enumerable: true},
                player: {value: new PlayerService(), enumerable: true},
                inputTypes: {value: inputTypes, enumerable: false},
                sceneTypes: {value: new SceneTypes(), enumerable: false},
                modules: {value: new SDKModuleCatalog(engineModules), enumerable: true}
            });
            Object.freeze(this);
        }
    }

    Object.defineProperty(globalThis, "NorthStar", {
        value: new NorthStarSDK(),
        writable: false, configurable: false, enumerable: false
    });
})();
