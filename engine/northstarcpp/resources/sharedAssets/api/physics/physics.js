    class Physics {
        // The native response provider exposes a DESTRUCTIVE contact drain.
        // Exactly one owner drains it per game update; consumers receive the
        // same immutable batch instead of competing for native messages.
        static #contactListeners = new Set();
        static #contactUpdate = null;
        static #contactStop = null;
        static #contactSnapshot = Object.freeze([]);
        static #contactEpoch = 0;

        static #dispatchContacts(dt) {
            if (typeof dt !== "number" || !Number.isFinite(dt) || dt < 0)
                return;
            const native = typeof engine.physics?.contacts?.pollContacts === "function" ?
                engine.physics.contacts.pollContacts(256) : [];
            if (!Array.isArray(native))
                throw new TypeError("Native physics contacts must be an array");
            const frozen = Object.freeze(native.map(contact => {
                if (!contact || typeof contact !== "object")
                    throw new TypeError("Malformed physics contact event");
                return Object.freeze({...contact});
            }));
            Physics.#contactSnapshot = frozen;
            Physics.#contactEpoch++;
            for (const listener of [...Physics.#contactListeners]) {
                if (!Physics.#contactListeners.has(listener)) continue;
                try { listener(frozen,dt); }
                catch(error) { Logger.warn("[engine.physics] contact subscriber: " +
                    String(error)); }
            }
        }

        static #detachContactDispatcher() {
            const update = Physics.#contactUpdate, stop = Physics.#contactStop;
            Physics.#contactUpdate = Physics.#contactStop = null;
            if (typeof update === "function") update();
            if (typeof stop === "function") stop();
        }

        static onContacts(listener) {
            if (typeof listener !== "function")
                throw new TypeError("Physics.onContacts requires a listener");
            if (typeof engine.on !== "function")
                throw new Error("Native engine event provider is unavailable");
            Physics.#contactListeners.add(listener);
            if (!Physics.#contactUpdate) {
                Physics.#contactUpdate = Events.on("update",
                    dt => Physics.#dispatchContacts(dt));
                Physics.#contactStop = Events.on("stop", () => Physics.resetContacts());
            }
            let active = true;
            return () => {
                if (!active) return false;
                active = false;
                Physics.#contactListeners.delete(listener);
                if (!Physics.#contactListeners.size)
                    Physics.#detachContactDispatcher();
                return true;
            };
        }

        static resetContacts() {
            Physics.#contactListeners.clear();
            Physics.#contactSnapshot = Object.freeze([]);
            Physics.#detachContactDispatcher();
        }

        static contacts() { return Physics.#contactSnapshot; }
        static get contactEpoch() { return Physics.#contactEpoch; }

        static raycast(from,to,ignoreIndex=-1) {
            Contracts.vector(from,"Raycast origin");
            Contracts.vector(to,"Raycast target");
            if(!Number.isSafeInteger(ignoreIndex)||ignoreIndex< -1)
                throw new RangeError("Invalid ignored scene index");
            Capabilities.require("physics.raycast");
            return engine.physics.queries.raycast(from.x,from.y,from.z,
                to.x,to.y,to.z,ignoreIndex);
        }
        static sweepSphereChannel(from,to,radius,channel,ignoreIndex=-1) {
            Contracts.vector(from,"Sweep origin");
            Contracts.vector(to,"Sweep target");
            Contracts.number(radius,"Sweep radius",Number.MIN_VALUE,100);
            Contracts.identifier(channel,"Collision channel");
            if(!Number.isSafeInteger(ignoreIndex)||ignoreIndex< -1)
                throw new RangeError("Invalid ignored scene index");
            if(typeof engine.physics?.queries?.sweepSphereChannel!=="function")
                throw new EngineCapabilityError("physics.sweepSphereChannel");
            return engine.physics.queries.sweepSphereChannel(
                from.x,from.y,from.z,to.x,to.y,to.z,radius,channel,ignoreIndex);
        }

        static sweepSphere(from, to, radius, ignoreIndex = -1) {
            Contracts.vector(from,"Sweep origin");
            Contracts.vector(to,"Sweep destination");
            if (!Number.isFinite(radius) || radius <= 0 || radius > 100 ||
                !Number.isSafeInteger(ignoreIndex) || ignoreIndex < -1)
                throw new RangeError("Invalid radius or ignored scene index");
            return engine.physics.queries.sweepSphere(
                from.x, from.y, from.z, to.x, to.y, to.z,
                radius, ignoreIndex);
        }
        // Native provider contact events; -1 represents the implicit
        // physics ground. Begin/End are delivered at most once per pair.
        static pollContacts(max=256) {
            if(!Number.isSafeInteger(max) || max<1 || max>256)
                throw new RangeError("Physics contact batch must be 1..256");
            if(typeof engine.physics?.contacts?.pollContacts!=="function")
                return [];
            // Compatibility: while a dispatcher is active, this is a
            // non-consuming view. Outside it, keep legacy polling available.
            return Physics.#contactUpdate ?
                Physics.#contactSnapshot.slice(0,max) :
                engine.physics.contacts.pollContacts(max);
        }
        static setBodyMaterial(index,{friction=.30,restitution=.42}={}) {
            if(!Number.isSafeInteger(index)||index<0 ||
               !Number.isFinite(friction)||friction<0||friction>1 ||
               !Number.isFinite(restitution)||restitution<0||restitution>1)
                throw new RangeError("Invalid physics body surface material");
            if(typeof engine.physics?.bodies?.setBodyMaterial!=="function")return false;
            return Boolean(engine.physics.bodies.setBodyMaterial(index,friction,restitution));
        }
        static applyImpulse(index, vector) {
            if (!Number.isSafeInteger(index) || index < 0)
                throw new RangeError("Physics index must be nonnegative");
            Contracts.vector(vector,"Impulse");
            return engine.physics.bodies.applyImpulse(index,
                vector.x, vector.y, vector.z);
        }
    }

    // Provider-agnostic FPS ballistics; weapon definitions only supply local
    // muzzle metadata. The renderer owns attachment transforms, while this
    // shared script owns sight-to-muzzle convergence and collision queries.
