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
