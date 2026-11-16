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
        /** VFS camera track; author motion using the Camera Timeline Editor. */
        static playTrack(path, loop = false, blendSeconds = 0.5) {
            if (typeof path !== "string" || path.length === 0)
                throw new TypeError("Camera track requires a VFS path");
            return engine.camera.playTrack(path, loop, blendSeconds);
        }
        /** Sequence several animated camera shots and return to gameplay. */
        static playCutscene(path, options = {}) {
            if (typeof path !== "string" || path.length === 0)
                throw new TypeError("Camera cutscene requires a VFS path");
            const { loop = false, entrySeconds = 0.55,
                    exitSeconds = 0.55 } = options;
            if (typeof loop !== "boolean" || !Number.isFinite(entrySeconds) ||
                entrySeconds < 0 || entrySeconds > 10 ||
                !Number.isFinite(exitSeconds) ||
                exitSeconds < 0 || exitSeconds > 10)
                throw new RangeError("Invalid camera cutscene options");
            return engine.camera.playCutscene(
                path, loop, entrySeconds, exitSeconds);
        }
        static stopCutscene(exitSeconds = 0.55) {
            return engine.camera.stopCutscene(exitSeconds);
        }
        static pauseCutscene(paused = true) {
            return engine.camera.pauseCutscene(paused);
        }
        static seekCutscene(seconds) {
            return engine.camera.seekCutscene(seconds);
        }
        static cutsceneState() { return engine.camera.cutsceneState(); }
        /** Filmic exposure is applied to the active Vulkan/D3D12 provider.
         * Focus/aperture/DoF/blur/letterbox are authored lens state and
         * become native GPU effects as provider support is added. */
        static lensState() { return engine.camera.lensState(); }
        /** Subscribe to authored animation/audio/dialogue/overlay/event cues.
         * The engine only emits data; the project owns what a cue means.
         * Returns a disposer; call it when the owning script stops. */
        static onCue(handler) {
            if (typeof handler !== "function")
                throw new TypeError("Camera.onCue requires a callback");
            return Events.on("camera.cues", payload => {
                const batch = typeof payload === "string"
                    ? JSON.parse(payload) : payload;
                if (!Array.isArray(batch) || batch.length > 8192)
                    throw new TypeError("Camera cue dispatch expects a bounded array");
                for (const cue of batch) {
                    if (!cue || typeof cue !== "object" ||
                        typeof cue.type !== "string" ||
                        typeof cue.time !== "number")
                        throw new TypeError("Camera cue payload is invalid");
                    handler(cue);
                }
            });
        }
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
                "name", "position", "size", "mass", "color", "asset", "layer", "parent", "collision"
            ]);
            for (const name of Object.keys(options)) {
                if (!allowed.has(name))
                    throw new RangeError("Unsupported Scene.spawn option: " + name);
            }
            const {name = "Runtime Entity", position, size = 1,
                mass = 0, color = 0x46b9d8, asset = "", layer = "Default",
                parent = null, collision = true} = options;
            if (typeof name !== "string" || !name ||
                typeof layer !== "string" || !layer ||
                typeof asset !== "string" ||
                !position || ![position.x, position.y, position.z].every(Number.isFinite) ||
                !Number.isFinite(size) || size <= 0 || size > 1000 ||
                !Number.isFinite(mass) || mass < 0 || mass > 1000000 ||
                typeof collision !== "boolean" ||
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
            if(!collision) args.push(false);
            const record = engine.scene.spawn(...args);
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

        /** Non-owning handle for a saved Scene Entity. Never instantiate an
         *  already-authored skeletal visual a second time. */
        static handleByName(name) {
            const record=Scene.findByName(name);
            if(!record)return null;
            const {id,index}=record;
            return Object.freeze({
                id,index,
                get alive() {
                    const current=Scene.get(index);
                    return Boolean(current && current.id===id);
                },
                setPosition(position) {Scene.moveStatic(index,position);return this;},
                setRotation(rotation) {Scene.rotateStatic(index,rotation);return this;}
            });
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
            const index = engine.player.controlledIndex();
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
            return engine.scene.moveStatic(index,
                position.x, position.y, position.z);
        }

        static rotateStatic(index, rotation) {
            if(!Number.isSafeInteger(index) || index<0 ||
               !rotation || ![rotation.x,rotation.y,rotation.z].every(Number.isFinite))
                throw new TypeError("Scene rotation requires a valid entity and finite Euler angles");
            if(Math.max(Math.abs(rotation.x),Math.abs(rotation.y),
                        Math.abs(rotation.z))>360000)
                throw new RangeError("Scene rotation exceeds supported limits");
            return engine.scene.rotateStatic(index,rotation.x,rotation.y,rotation.z);
        }

        static setColor(index, rgb) {
            if (!Number.isInteger(rgb) || rgb < 0 || rgb > 0xffffff)
                throw new RangeError("Scene RGB color must be 0x000000..0xFFFFFF");
            return engine.scene.setColor(index, rgb);
        }

        /** Render presentation of an existing skinned Entity, not a prefab swap.
         *  The camera rig stays an independent scene object. */
        static setVisualMode(index, mode, settings = {}) {
            if (!Number.isSafeInteger(index) || index < 0 ||
                (mode !== "world" && mode !== "first_person_arms"))
                throw new TypeError("Invalid scene visual mode");
            if (settings == null || typeof settings !== "object")
                throw new TypeError("Invalid camera presentation settings");
            const forward = settings.forward ?? 0.55;
            const drop = settings.drop ?? 0.98;
            const pitch = settings.armPitch ?? 55;
            if (![forward, drop, pitch].every(Number.isFinite) ||
                forward < 0 || forward > 3 || drop < 0 || drop > 3 ||
                Math.abs(pitch) > 180)
                throw new RangeError("Invalid first-person arm presentation");
            return engine.scene.setVisualMode(index, mode, forward, drop, pitch);
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
            return engine.scene.attachBone(childIndex,parentIndex,bone,...pos,...angles,alignment);
        }

        static detachFromBone(childIndex) {
            if(!Number.isSafeInteger(childIndex)||childIndex<0)
                throw new RangeError("Invalid scene bone attachment index");
            return engine.scene.detachBone(childIndex);
        }

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
        /** The currently loaded project-relative scene path. */
        static scene() { return engine.session.scene(); }
        /** Request a safe scene swap after the current script callback. */
        static loadScene(path) {
            if (typeof path !== "string" || !/^[a-zA-Z0-9_./-]+\.nscn$/.test(path) ||
                path.split("/").some(part => part === ".." || !part))
                throw new TypeError("Session.loadScene expects project-relative .nscn");
            return engine.session.loadScene(path);
        }
    }

    // Game-agnostic UI renderer. Project scripts own every semantic layer:
    // quests, health, inventory, notifications, and screen advertising.
    // Only engine.ui.apply(document) crosses into native C++.
    class UI {
        static #layers = new Map();
        static #RootType = null;
        static #surfaces = new Map();

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

        static hasSurface(entityId) { return UI.#surfaces.has(String(entityId)); }
        static setTitle(text) { return engine.ui.setTitle(text); }
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
            return engine.ui.apply({target:"screen",nodes});
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
            engine.ui.apply(data);
            UI.#surfaces.set(id,signature);
        }
        static unmountSurface(entityId) {
            const id=String(entityId);
            if(!UI.#surfaces.has(id))return;
            engine.ui.apply({target:"world",entityId:id,nodes:[]});
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
        static get index() { return engine.player.controlledIndex(); }
        static get entityId() { return engine.player.controlledEntity(); }
        static get hasPawn() { return Player.index >= 0; }
        static requirePawn() {
            const pawn = Scene.findPawn();
            if (!pawn)
                throw new Error("No possessed Pawn. Add a Pawn component and enable Auto Possess Local.");
            return pawn;
        }
        static possess(id) { return engine.player.possess(String(id)); }
        static unpossess() { return engine.player.unpossess(); }
        static sprint(active) { return engine.player.sprint(Boolean(active)); }
        static setIndex(index) { return NorthStar.player.setIndex(index); }
        static get paused() { return NorthStar.player.paused; }
        static setPaused(value) { return NorthStar.player.setPaused(value); }
        static move(x, z) { return engine.player.move(x, z); }
        static motion() { return engine.player.motion(); }
        static ground() { return engine.player.ground(); }
        static jump() { return engine.player.jump(); }
        static eye() { return engine.player.eye(); }
        static setEyeHeight(height) { return engine.player.setEyeHeight(height); }
    }

    // Base lifecycle for independent root-script classes. A root class
    // registers once and keeps its mutable state in private instance fields.
    class Audio {
        // A provider-neutral request for one PCM clip from a NEF8/NAC archive.
        // The native host caches the decompressed container per script session.
        static playWav(path) {
            if(typeof path !== "string" ||
               !/^audio\/[a-zA-Z0-9_.\/-]+\.wav$/.test(path) ||
               path.includes(".."))
                throw new TypeError("Audio.playWav requires audio/*.wav");
            return engine.audio.play(path);
        }
        static playNac(path, action, variation = 0, gain = 1) {
            if (typeof path !== "string" ||
                !/^audio\/[a-zA-Z0-9_.\/-]+\.nac$/.test(path) ||
                path.includes("..") || !/^[a-zA-Z0-9_]{1,48}$/.test(action) ||
                !Number.isSafeInteger(variation) || variation < 0 ||
                !Number.isFinite(gain) || gain < 0 || gain > 2)
                throw new TypeError("Invalid NAC playback request");
            return engine.audio.playNac(path, action, variation, gain);
        }
    }

    class Script {
        #scope = null;

        get installed() { return this.#scope !== null; }

        install() {
            if (this.installed)
                throw new Error("Root script already installed");
            const scope = Events.scope();
            try {
                scope.on("start", () => this.start());
                scope.on("update", dt => this.update(dt));
                scope.on("stop", () => this.stop());
            } catch (error) {
                scope.dispose();
                throw error;
            }
            this.#scope = scope;
            return this;
        }

        // Explicit cleanup, useful in editor PIE and reconfigurable projects.
        // Normal stop() is still dispatched through the native event bus.
        uninstall() {
            if (!this.#scope) return false;
            this.#scope.dispose();
            this.#scope = null;
            return true;
        }

        start() {}
        update(_dt) {}
        stop() {}
    }

    // A reusable gameplay adapter for roots that control a possessed Pawn.
    // Engine ownership is always supplied by PlayerController, never
    // inferred from an object name, boolean, or scene-wide iteration.
    class PlayerScript extends Script {
        get enabled() { return Player.enabled; }
        get active() { return Player.enabled && Player.index >= 0; }
        get playing() { return this.active && !Player.paused; }
        get options() { return Player.enabled ? Player.config : null; }
    }

    class NorthStarSDK {
        constructor() {
            const inputTypes = new InputTypes();
            const engineModules = {
                Camera, Input, Events, Audio, Character, Player, Script,
                PlayerScript, InputAction, Scene, Physics, Session, UI, Logger
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
