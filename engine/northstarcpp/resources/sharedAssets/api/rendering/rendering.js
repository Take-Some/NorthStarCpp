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
