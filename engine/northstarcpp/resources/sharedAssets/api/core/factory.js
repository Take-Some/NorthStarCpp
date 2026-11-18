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
