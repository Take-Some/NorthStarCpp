    // OOP execution lifetime shared by player, entity, and project scripts.
    // Engine callbacks never retain disposed scripts; owned resources are
    // released in reverse registration order on stop/uninstall/failure.
    class Script {
        #scope=null;
        #context=null;
        #options;
        #uninstalling=false;

        constructor(options={}) {
            this.#options=Contracts.options(options,"Script options");
        }
        get installed() { return this.#scope!==null; }
        get context() { return this.#context; }
        get entity() { return this.#context?.entity??null; }
        listen(name,handler) {
            if(!this.#context)throw new Error("Install script before subscribing");
            return this.#context.listen(name,handler);
        }
        own(resource) {
            if(!this.#context)throw new Error("Install script before owning resources");
            return this.#context.own(resource);
        }
        after(seconds,callback) {
            if(!this.#context)throw new Error("Install script before scheduling");
            return this.#context.after(seconds,callback);
        }
        every(seconds,callback) {
            if(!this.#context)throw new Error("Install script before scheduling");
            return this.#context.every(seconds,callback);
        }

        #invoke(phase,callback) {
            try { callback(); }
            catch(error) {
                try {
                    Logger.warn("[engine.script] "+this.constructor.name+
                        "."+phase+": "+String(error?.stack??error));
                } catch(_) {}
                this.uninstall();
            }
        }

        install() {
            if(this.installed)throw new Error("Root script already installed");
            Time.attach();
            const scope=Events.scope();
            const context=new ScriptContext(this.#options);
            this.#scope=scope;
            this.#context=context;
            try {
                scope.on("start",()=>this.#invoke("start",()=>this.start()));
                scope.on("update",dt=>this.#invoke("update",()=>{
                    context.tick(dt);
                    this.update(dt);
                }));
                scope.on("stop",()=>{
                    this.#invoke("stop",()=>this.stop());
                    this.uninstall();
                });
            } catch(error) {
                this.uninstall();
                throw error;
            }
            return this;
        }

        uninstall() {
            if(!this.#scope || this.#uninstalling)return false;
            this.#uninstalling=true;
            const scope=this.#scope, context=this.#context;
            try {
                scope.dispose();
                // onDestroy runs while context is still readable.
                try { this.onDestroy(); }
                catch(error) {
                    try { Logger.warn("[engine.script] onDestroy: "+String(error)); }
                    catch(_) {}
                }
                context?.dispose();
                Time.release();
            } finally {
                this.#scope=null;
                this.#context=null;
                this.#uninstalling=false;
            }
            return true;
        }
        start() {}
        update(_dt) {}
        stop() {}
        onDestroy() {}
    }

    class PlayerScript extends Script {
        get enabled() { return Player.enabled; }
        get active() { return Player.enabled && Player.index>=0; }
        get playing() { return this.active && !Player.paused; }
        get options() { return Player.enabled ? Player.config : null; }
    }

    // Entity is a stable, non-owning handle supplied by scene or Factory.
    // Native automatic attachment to Scene ECS components is a separate
    // host capability; no fake getComponent() is installed here.
    class EntityScript extends Script {
        constructor({entity,owner=null}={}) {
            if(!entity || typeof entity.id!=="string" ||
               !Number.isSafeInteger(entity.index) ||
               typeof entity.alive!=="boolean")
                throw new TypeError("EntityScript requires a live Entity handle");
            if(!entity.alive)throw new Error("Cannot attach to a dead Entity");
            super({entity,owner:owner??entity});
        }
        get entity() {
            const entity=super.entity;
            if(!entity)throw new Error("EntityScript is not installed");
            if(!entity.alive)throw new Error("Bound Entity was destroyed");
            return entity;
        }
        start() { this.onStart(); }
        update(dt) {
            if(!this.entity.alive)return;
            this.onUpdate(dt);
        }
        stop() { this.onStop(); }
        onStart() {}
        onUpdate(_dt) {}
        onStop() {}
    }

    class ComponentScript extends EntityScript {
        #component;
        constructor({component, ...options}={}) {
            if(typeof component!=="string" || !component)
                throw new TypeError("ComponentScript needs a component name");
            super(options);
            this.#component=component;
        }
        get componentName() { return this.#component; }
        get component() {
            return ECS.get(this.entity,this.#component);
        }
    }
