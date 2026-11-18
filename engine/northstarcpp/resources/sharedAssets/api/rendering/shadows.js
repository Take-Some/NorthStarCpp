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
