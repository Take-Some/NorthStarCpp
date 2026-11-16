// NorthStarCpp engine-owned UI root script.
//
// Virtual path: scripts/ui/UI.js
// Loaded once in every script context, before project scripts/main.js.
// Sealed engine infrastructure; projects compose UI roots instead of replacing this class.
// This is a scripting convenience layer, NOT a native gameplay subsystem.
//
// Low-level native operation: engine.ui.apply({target:"screen",nodes:[...]})
// High-level API: UI.createRoot("hud").panel(...).text(...).mount()
(() => {
    "use strict";

    const UI = globalThis.NorthStar?.modules?.engine?.UI;
    const Events = globalThis.NorthStar?.modules?.engine?.Events;
    if (!UI || typeof UI.registerRoot !== "function")
        throw new Error("UI root requires NorthStar engine bootstrap");

    const IDENTIFIER = /^[a-zA-Z][a-zA-Z0-9_.-]{0,63}$/;
    const LAYER = /^[a-zA-Z][a-zA-Z0-9_-]{0,47}$/;
    const KINDS = Object.freeze(["panel", "text", "button", "image", "input"]);

    function properties(input) {
        if (!input || typeof input !== "object" || Array.isArray(input))
            throw new TypeError("UI element props must be an object");
        const result = {};
        const entries = Object.entries(input);
        if (entries.length > 48)
            throw new RangeError("UI node cannot have more than 48 properties");
        for (const [key, value] of entries) {
            if (!IDENTIFIER.test(key) ||
                !(typeof value === "string" || typeof value === "boolean" ||
                  (typeof value === "number" && Number.isFinite(value))))
                throw new TypeError("UI props must contain string/bool/finite number values");
            if (typeof value === "string" && value.length > 2048)
                throw new RangeError("UI property string exceeds 2048 characters");
            result[key] = value;
        }
        return result;
    }

    // Scoped authoring facade: a section's descendants inherit its parent.
    // This affects only the JavaScript builder; native engine.ui stays generic.
    class UIGroup {
        #root;
        #parent;
        constructor(root, parent) {
            this.#root = root;
            this.#parent = parent;
        }
        panel(id, props = {}) {
            this.#root.panel(id, props, this.#parent); return this;
        }
        text(id, value, props = {}) {
            this.#root.text(id, value, props, this.#parent); return this;
        }
        button(id, value, props = {}) {
            this.#root.button(id, value, props, this.#parent); return this;
        }
        image(id, source, props = {}) {
            this.#root.image(id, source, props, this.#parent); return this;
        }
        input(id, props = {}) {
            this.#root.input(id, props, this.#parent); return this;
        }
        progress(id, fraction, props = {}) {
            this.#root.progress(id, fraction, props, this.#parent); return this;
        }
        group(id, props, build) {
            this.#root.group(id, props, build, this.#parent); return this;
        }
    }

    class UIRoot {
        #layer;
        #elements = new Map();
        #mounted = false;
        #revision = 0;
        #sentRevision = -1;
        #batchDepth = 0;
        #batchDirty = false;
        #handlers = new Map();
        #unsubscribe = null;
        #surface = null;

        constructor(layer = "default", options = {}) {
            if (typeof layer !== "string" || !LAYER.test(layer))
                throw new TypeError("UI root layer must be a valid identifier");
            this.#layer = layer;
            if(options.target === "world")
                this.#surface = Object.freeze({...options});
        }

        get name() { return this.#layer; }
        get mounted() { return this.#mounted; }
        get target() { return this.#surface ? "world" : "screen"; }
        get size() { return this.#elements.size; }

        /** Subscriptions reference stable script IDs, not native handles. */
        on(id, event, callback) {
            if (!this.#elements.has(id))
                throw new RangeError("UI event target does not exist: " + id);
            if (!["click","change","focus","blur","key","pointer","press"].includes(event))
                throw new RangeError("Unsupported UI event type: " + event);
            if (typeof callback !== "function")
                throw new TypeError("UI event callback must be a function");
            const key = event + ":" + id;
            let listeners = this.#handlers.get(key);
            if (!listeners) this.#handlers.set(key,listeners = new Set());
            listeners.add(callback);
            this.#listen();
            return this;
        }
        onClick(id, callback) { return this.on(id,"click",callback); }
        onPointer(id, callback) { return this.on(id,"pointer",callback); }
        onPress(id, callback) { return this.on(id,"press",callback); }
        onChange(id, callback) { return this.on(id,"change",callback); }
        off(id,event,callback) {
            const key=event+":"+id;
            const listeners=this.#handlers.get(key);
            if (!listeners) return this;
            listeners.delete(callback);
            if (!listeners.size) this.#handlers.delete(key);
            if (!this.#handlers.size) this.#stopListening();
            return this;
        }

        #listen() {
            if (!this.#mounted || this.#unsubscribe || !this.#handlers.size)
                return;
            if (!Events || typeof Events.on !== "function")
                throw new Error("Events service unavailable for UI callbacks");
            this.#unsubscribe = Events.on("ui.event", message => {
                if (!this.#mounted || !message ||
                    typeof message.id !== "string" ||
                    !message.id.startsWith(this.#layer + ".")) return;
                const id=message.id.slice(this.#layer.length+1);
                const handlers=this.#handlers.get(message.name + ":" + id);
                if (!handlers) return;
                for (const callback of [...handlers]) {
                    try { callback(message); }
                    catch(error) {
                        try { NorthStar.modules.engine.Logger.warn(
                            "UI callback " + message.id + ": " + String(error));
                        } catch(_) {}
                    }
                }
            });
        }
        #stopListening() {
            if (this.#unsubscribe) this.#unsubscribe();
            this.#unsubscribe=null;
        }

        /** Check whether an element exists without querying native UI. */
        has(id) { return this.#elements.has(id); }

        /** Immutable-by-copy snapshot, suitable for debugging and inspection. */
        snapshot() {
            return Array.from(this.#elements.values(), node => ({
                id: node.id, type: node.type, parent: node.parent,
                props: {...node.props}
            }));
        }

        /** Build a panel and its children without repeating parent IDs. */
        group(id, props = {}, build = null, parent = "") {
            if (build !== null && typeof build !== "function")
                throw new TypeError("UI.group build must be a function");
            return this.batch(root => {
                root.panel(id, props, parent);
                if (build) build(new UIGroup(root, id));
            });
        }

        // Generic primitives — do not add mission/HP/advertising semantics.
        add(type, id, props = {}, parent = "") {
            if (!KINDS.includes(type) || typeof id !== "string" ||
                !IDENTIFIER.test(id) ||
                this.#layer.length + 1 + id.length > 96 ||
                this.#elements.has(id) ||
                (parent !== "" &&
                    (typeof parent !== "string" || !this.#elements.has(parent))))
                throw new TypeError("UI element type, id or parent is invalid");
            if (this.#elements.size >= 256)
                throw new RangeError("UI root cannot contain more than 256 elements");
            this.#elements.set(id, {
                id, type, parent, props: properties(props)
            });
            this.#sync();
            return this;
        }

        panel(id, props = {}, parent = "") {
            return this.add("panel", id, props, parent);
        }

        text(id, value, props = {}, parent = "") {
            if (typeof value !== "string")
                throw new TypeError("UI text content must be a string");
            return this.add("text", id, {...props, text:value}, parent);
        }

        button(id, value, props = {}, parent = "") {
            if (typeof value !== "string")
                throw new TypeError("UI button caption must be a string");
            return this.add("button", id, {...props, text:value}, parent);
        }

        image(id, source, props = {}, parent = "") {
            if (typeof source !== "string" || !source ||
                source.includes(".."))
                throw new TypeError("UI image requires a safe asset path");
            return this.add("image", id, {...props, source}, parent);
        }

        input(id, props = {}, parent = "") {
            return this.add("input", id, props, parent);
        }

        // Pure UI widget: no gameplay meaning. The calling project supplies
        // an arbitrary numeric fraction (health, loading, progress, etc).
        progress(id, fraction, props = {}, parent = "") {
            if (typeof fraction !== "number" || !Number.isFinite(fraction) ||
                fraction < 0 || fraction > 1)
                throw new RangeError("UI progress fraction must be 0..1");
            const width = props.width ?? 240;
            const height = props.height ?? 14;
            if (!Number.isFinite(width) || width <= 0 ||
                !Number.isFinite(height) || height <= 0)
                throw new RangeError("Invalid UI progress dimensions");
            return this.group(id, {
                width, height, padding:0, gap:0,
                background:"#293746e5", ...props
            }, group => group.panel(id + ".fill", {
                width: Math.max(0, Math.round(width * fraction)),
                height, padding:0, gap:0, background:"#f0bd4fff"
            }), parent);
        }

        set(id, changes) {
            const item = this.#elements.get(id);
            if (!item)
                throw new RangeError("Unknown UI element: " + id);
            const validated = properties(changes);
            if (Object.entries(validated).every(
                ([key,value]) => Object.is(item.props[key],value))) return this;
            item.props = {...item.props,...validated};
            this.#sync();
            return this;
        }

        setText(id, value) {
            if (typeof value !== "string")
                throw new TypeError("UI text content must be a string");
            return this.set(id, {text:value});
        }

        setProgress(id, fraction) {
            if (typeof fraction !== "number" || !Number.isFinite(fraction) ||
                fraction < 0 || fraction > 1)
                throw new RangeError("UI progress fraction must be 0..1");
            const track = this.#elements.get(id);
            const fill = this.#elements.get(id + ".fill");
            if (!track || !fill || fill.parent !== id)
                throw new RangeError("UI progress element not found");
            return this.set(id + ".fill", {
                width: Math.round(track.props.width * fraction)
            });
        }

        remove(id) {
            if (!this.#elements.has(id))
                return this;
            // Remove descendants too; no dangling tree references.
            const removed = new Set([id]);
            let grew = true;
            while (grew) {
                grew = false;
                for (const item of this.#elements.values()) {
                    if (removed.has(item.parent) && !removed.has(item.id)) {
                        removed.add(item.id);
                        grew = true;
                    }
                }
            }
            for (const key of removed)
                this.#elements.delete(key);
            this.#sync();
            return this;
        }

        /**
         * Atomic update. A failed callback restores the old tree and
         * avoids publishing partial UI changes to engine.ui.
         * Nested batches coalesce into the outermost native submission.
         */
        batch(fn) {
            if (typeof fn !== "function")
                throw new TypeError("UI.batch expects a callback");
            const snapshot = new Map(Array.from(this.#elements,
                ([id,node]) => [id,{...node,props:{...node.props}}]));
            const revision = this.#revision;
            const dirty = this.#batchDirty;
            this.#batchDepth++;
            try {
                fn(this);
            } catch(error) {
                this.#elements = snapshot;
                this.#revision = revision;
                this.#batchDirty = dirty;
                throw error;
            } finally {
                this.#batchDepth--;
            }
            if (this.#batchDepth === 0 && this.#batchDirty) {
                this.#batchDirty = false;
                if (this.#mounted) this.mount();
            }
            return this;
        }

        mount() {
            const present = this.#surface ?
                UI.hasSurface(this.#surface.entityId) :
                UI.hasLayer(this.#layer);
            if (this.#mounted && present &&
                this.#sentRevision === this.#revision) return this;
            if (this.#surface)
                UI.mountSurface(this.#surface.entityId,this.#layer,
                    this.snapshot(),this.#surface);
            else UI.mount(this.#layer,this.snapshot());
            this.#mounted = true;
            this.#sentRevision = this.#revision;
            this.#listen();
            return this;
        }

        unmount() {
            if (this.#mounted) {
                if (this.#surface) UI.unmountSurface(this.#surface.entityId);
                else UI.unmount(this.#layer);
            }
            this.#mounted = false;
            this.#sentRevision = -1;
            this.#stopListening();
            return this;
        }

        clear() {
            if (this.#elements.size) {
                this.#elements.clear();
                this.#sync();
            }
            return this;
        }

        dispose() {
            this.unmount();
            this.#elements.clear();
            this.#handlers.clear();
        }

        #sync() {
            this.#revision++;
            if (this.#batchDepth) {
                this.#batchDirty = true;
            } else if (this.#mounted) {
                this.mount();
            }
        }
    }

    // One replacement point per script context; no extra native methods.
    UI.registerRoot(UIRoot);
})();