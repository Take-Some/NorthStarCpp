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
