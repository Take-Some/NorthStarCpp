    class EventScope {
        #subscriptions = new Set();
        #disposed = false;

        on(name, handler) {
            if (this.#disposed)
                throw new Error("Event scope has already been disposed");
            const unsubscribe = Events.on(name, handler);
            this.#subscriptions.add(unsubscribe);
            let active = true;
            return () => {
                if (!active) return false;
                active = false;
                this.#subscriptions.delete(unsubscribe);
                return unsubscribe();
            };
        }

        dispose() {
            if (this.#disposed) return;
            this.#disposed = true;
            for (const unsubscribe of this.#subscriptions)
                unsubscribe();
            this.#subscriptions.clear();
        }
    }

    class Events {
        static #contracts = new Map();
        static #subscriptions = new Map();
        static #emitted = new Map();
        static #recent = [];
        static #sequence = 0;
        static #validateName(name) {
            if (typeof name !== "string" ||
                !/^[a-zA-Z_][a-zA-Z0-9_.:-]{0,95}$/.test(name))
                throw new TypeError("Invalid event identifier");
        }

        static define(name, validator = null) {
            Events.#validateName(name);
            if (validator !== null && typeof validator !== "function")
                throw new TypeError("Validator must be a function");
            if (Events.#contracts.size >= 256)
                throw new RangeError("Too many event contracts");
            if (Events.#contracts.has(name))
                throw new Error("Event already declared: " + name);
            Events.#contracts.set(name, validator);
        }

        static on(name, handler) {
            Events.#validateName(name);
            if (typeof handler !== "function")
                throw new TypeError("Event listener must be a function");
            const nativeDispose = engine.on(name, handler);
            let records = Events.#subscriptions.get(name);
            if (!records) {
                records = new Map();
                Events.#subscriptions.set(name, records);
            }
            let active = true;
            const dispose = () => {
                if (!active) return false;
                active = false;
                if (typeof nativeDispose === "function") nativeDispose();
                else if (typeof engine.off === "function") engine.off(name, handler);
                records.delete(dispose);
                if (!records.size) Events.#subscriptions.delete(name);
                return true;
            };
            records.set(dispose, handler);
            return dispose;
        }
        static off(name, handler) {
            Events.#validateName(name);
            const records = Events.#subscriptions.get(name);
            if (!records) return false;
            let removed = false;
            for (const [dispose, callback] of [...records]) {
                if (callback === handler) removed = dispose() || removed;
            }
            return removed;
        }
        static emit(name, payload = null) {
            Events.#validateName(name);
            const validate = Events.#contracts.get(name);
            if (validate && !validate(payload))
                throw new TypeError("Invalid event payload: " + name);
            const result = engine.emit(name, payload);
            // Only events published through Events.emit are recorded;
            // raw native "start"/"update" signals bypass this lightweight log.
            if (Events.#emitted.size < 256 || Events.#emitted.has(name))
                Events.#emitted.set(name, (Events.#emitted.get(name) || 0) + 1);
            Events.#recent.push(Object.freeze({sequence: ++Events.#sequence, name}));
            if (Events.#recent.length > 64) Events.#recent.shift();
            return result;
        }

        static scope() { return new EventScope(); }

        static stats() {
            const subscriptions = {};
            for (const [name, records] of Events.#subscriptions)
                subscriptions[name] = records.size;
            return Object.freeze({
                declared: Events.#contracts.size,
                emitted: Events.#sequence,
                emittedByEvent: Object.freeze(
                    Object.fromEntries(Events.#emitted)),
                subscribers: Object.values(subscriptions).reduce(
                    (sum, count) => sum + count, 0),
                byEvent: Object.freeze(subscriptions)
            });
        }
        static history(limit = 32) {
            if (!Number.isSafeInteger(limit) || limit < 1 || limit > 64)
                throw new RangeError("Event history limit must be 1..64");
            return Object.freeze(Events.#recent.slice(-limit));
        }

        static once(name, handler) {
            if (typeof handler !== "function")
                throw new TypeError("Event listener must be a function");
            let unsubscribe;
            unsubscribe = Events.on(name, value => {
                unsubscribe();
                handler(value);
            });
            return unsubscribe;
        }
    }
