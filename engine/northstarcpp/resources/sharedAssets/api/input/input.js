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
