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
