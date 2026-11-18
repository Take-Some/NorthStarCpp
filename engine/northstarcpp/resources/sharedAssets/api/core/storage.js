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
