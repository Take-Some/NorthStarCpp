    class ScriptContext {
        #entity;
        #owner;
        #disposers=[];
        #disposed=false;
        #scheduler;

        constructor({entity=null,owner=null}={}) {
            this.#entity=entity;
            this.#owner=owner;
            this.#scheduler=new Scheduler();
            Object.freeze(this);
        }
        get entity() { return this.#entity; }
        get owner() { return this.#owner; }
        get active() { return !this.#disposed; }
        get scheduler() { return this.#scheduler; }
        #requireActive() {
            if (this.#disposed) throw new Error("Script context is disposed");
        }
        own(resource) {
            this.#requireActive();
            const cleanup = typeof resource==="function" ? resource :
                resource && typeof resource.dispose==="function" ?
                    () => resource.dispose() :
                resource && typeof resource.cancel==="function" ?
                    () => resource.cancel() :
                resource && typeof resource.uninstall==="function" ?
                    () => resource.uninstall() :
                resource && typeof resource.destroy==="function" ?
                    () => resource.destroy() : null;
            if (!cleanup) throw new TypeError(
                "Owned resource needs dispose(), cancel(), uninstall() or destroy()");
            this.#disposers.push(cleanup);
            return resource;
        }
        listen(event, callback) {
            this.#requireActive();
            return this.own(Events.on(event,callback));
        }
        after(seconds, callback) {
            this.#requireActive();
            return this.#scheduler.after(seconds,callback);
        }
        every(seconds, callback) {
            this.#requireActive();
            return this.#scheduler.every(seconds,callback);
        }
        tick(dt) {
            if (!this.#disposed) this.#scheduler.update(dt);
        }
        dispose() {
            if (this.#disposed) return false;
            this.#disposed=true;
            this.#scheduler.dispose();
            const disposers=this.#disposers.splice(0).reverse();
            for(const cleanup of disposers) {
                try { cleanup(); }
                catch(error) { Logger.warn("[engine.script] cleanup: "+String(error)); }
            }
            return true;
        }
    }
