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
