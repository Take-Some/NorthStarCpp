    class UI {
        static #layers = new Map();
        static #RootType = null;
        static #surfaces = new Map();
        static #assetRoots = new Map();
        static #assetWatchEnabled = false;
        static #assetWatchElapsed = 0;
        static #animationClock = 0;

        // The default authoring implementation is loaded from
        // scripts/ui/UI.js before any project script. A project can override
        // that root module without replacing the native UI provider.
        static registerRoot(RootType) {
            if (UI.#RootType !== null)
                throw new Error("UI root script has already been registered");
            if (typeof RootType !== "function" ||
                typeof RootType.prototype.mount !== "function" ||
                typeof RootType.prototype.unmount !== "function")
                throw new TypeError("UI root must implement mount/unmount");
            UI.#RootType = RootType;
        }

        static createRoot(layer = "default") {
            if (UI.#RootType === null)
                throw new Error("scripts/ui/UI.js is not registered");
            return new UI.#RootType(layer);
        }

        /** Build a generic UI document rendered on an Entity's front face. */
        static createSurface(entityId, options = {}) {
            if (!UI.#RootType)
                throw new Error("scripts/ui/UI.js is not registered");
            return new UI.#RootType(options.layer ?? "surface",
                {...options,entityId:String(entityId),target:"world"});
        }

        /**
         * Instantiate a declarative .nui UI asset from the shared/project VFS.
         * Project assets override identically named engine assets. The returned
         * UIRoot is editable at runtime and can register click/event handlers.
         * Call .mount() when bindings have been attached.
         *
         * UI.loadAsset("ui/main_menu.nui", {layer:"menu"})
         * UI.loadAsset("ui/terminal.nui", {layer:"terminal",
         *     entityId:targetEntityId})
         */
        static loadAsset(path, options = {}) {
            if(typeof path!=="string" ||
               !/^ui\/[a-zA-Z0-9_.\/-]+\.nui$/.test(path) ||
               path.split("/").some(segment=>segment==="." || segment===".." ||
                                              segment.length===0))
                throw new TypeError("UI assets must be relative ui/*.nui files");
            const source=engine.ui.documents.readAsset(path);
            if(typeof source!=="string" || source.length>262144)
                throw new TypeError("Invalid UI asset contents");
            const doc=JSON.parse(source);
            if(!doc || !["northstar.ui/1","northstar.ui/2"].includes(doc.schema) ||
               !["screen","world"].includes(doc.target) ||
               !Array.isArray(doc.nodes) || doc.nodes.length>256)
                throw new TypeError("Unsupported or malformed NorthStar UI document");
            // UI/2: design tokens and reusable named styles are source-level
            // authoring metadata, never new engine.ui ABI or gameplay logic.
            const version2=doc.schema==="northstar.ui/2";
            const tokens=version2?(doc.tokens??{}):{};
            const styles=version2?(doc.styles??{}):{};
            const plain=value=>value!==null && typeof value==="object" &&
                !Array.isArray(value);
            const ident=/^[a-zA-Z][a-zA-Z0-9_.-]{0,63}$/;
            if(!plain(tokens)||!plain(styles) ||
               Object.keys(tokens).length>128 ||
               Object.keys(styles).length>128)
                throw new TypeError("Invalid UI token or style dictionary");
            for(const [key,value] of Object.entries(tokens))
                if(!ident.test(key) ||
                   !(typeof value==="string" || typeof value==="boolean" ||
                     (typeof value==="number" && Number.isFinite(value))))
                    throw new TypeError("Invalid UI design token: "+key);
            for(const [key,rule] of Object.entries(styles))
                if(!ident.test(key) || !plain(rule) ||
                   Object.keys(rule).length>48)
                    throw new TypeError("Invalid UI style: "+key);
            const valueOf=(value)=>{
                let resolved=value;
                if(plain(value) && Object.keys(value).length===1 &&
                   Object.prototype.hasOwnProperty.call(value,"token")) {
                    const name=value.token;
                    if(typeof name!=="string" ||
                       !Object.prototype.hasOwnProperty.call(tokens,name))
                        throw new TypeError("Unknown UI design token: "+name);
                    resolved=tokens[name];
                }
                if(typeof resolved==="number" &&
                   (!Number.isFinite(resolved) || Math.abs(resolved)>1000000))
                    throw new TypeError("UI numeric property exceeds runtime bounds");
                return resolved;
            };
            const layer=options.layer??doc.layer??"asset";
            const surface=doc.target==="world";
            if(surface && options.entityId===undefined)
                throw new TypeError("World UI asset requires options.entityId");
            if(!surface && options.entityId!==undefined)
                throw new TypeError("Screen UI asset does not accept entityId");
            const root=options.__reuseRoot??(surface?
                UI.createSurface(options.entityId,{
                    layer,width:doc.width??512,height:doc.height??256,
                    worldWidth:doc.worldWidth??1.8,
                    worldHeight:doc.worldHeight??1
                }):UI.createRoot(layer));
            const components=doc.components??{};
            const actions=doc.actions??[];
            const animations=doc.animations??[];
            const breakpoints=doc.breakpoints??[];
            if(!plain(components) || Object.keys(components).length>64 ||
               !Array.isArray(actions)||actions.length>128 ||
               !Array.isArray(animations)||animations.length>128 ||
               !Array.isArray(breakpoints)||breakpoints.length>64)
                throw new TypeError("Invalid XML component/action/animation/breakpoint metadata");
            const viewport=typeof engine.ui.viewport.viewportSize==="function"?
                engine.ui.viewport.viewportSize():{};
            const viewportWidth=options.viewportWidth??viewport?.width??1920;
            const active=breakpoints.filter(rule=>
                viewportWidth>=rule.minWidth && viewportWidth<=rule.maxWidth);
            const responsive=new Map();
            for(const rule of active)
                for(const [id,overrides] of Object.entries(rule.overrides))
                    responsive.set(id,{...(responsive.get(id)??{}),...overrides});
            const elements=[];
            for(const node of doc.nodes) {
                    if(!node || typeof node!=="object" ||
                       !Array.isArray(Object.keys(node)) ||
                       !["panel","text","button","image","input"].includes(node.type))
                        throw new TypeError("Unsupported UI asset element");
                    const style=node.style;
                    if(style!==undefined &&
                       (typeof style!=="string" || !ident.test(style) ||
                        !Object.prototype.hasOwnProperty.call(styles,style)))
                        throw new TypeError("Unknown UI style: "+String(style));
                    const component=node.component;
                    let inherited={};
                    if(component!==undefined){
                        if(typeof component!=="string" || !ident.test(component) ||
                           !plain(components[component]) ||
                           components[component].type!==node.type)
                            throw new TypeError("Unknown reusable .nui component: "+component);
                        inherited=components[component].props??{};
                        if(!plain(inherited))
                            throw new TypeError("UI component props must be an object");
                    }
                    const props={...inherited,...(style?styles[style]:{}),
                        ...(node.props??{}),...(responsive.get(node.id)??{})};
                    for(const [name,value] of Object.entries(props))
                        props[name]=valueOf(value);
                    elements.push({type:node.type,id:node.id,props,parent:node.parent??""});
                }
            const names=new Set(elements.map(node=>node.id));
            const animationsByName=new Map();
            for(const spec of animations) {
                if(typeof spec.name!=="string" || !ident.test(spec.name) ||
                   animationsByName.has(spec.name) || !names.has(spec.id) ||
                   !["x","y","width","height","font_scale"].includes(spec.property) ||
                   !Number.isFinite(spec.from) || !Number.isFinite(spec.to) ||
                   !Number.isFinite(spec.duration)||spec.duration<=0||spec.duration>30 ||
                   !["linear","easeOutCubic","easeInOut"].includes(spec.easing) ||
                   !["manual","mount"].includes(spec.trigger))
                    throw new TypeError("Invalid .nui animation definition");
                animationsByName.set(spec.name,spec);
            }
            for(const action of actions) {
                if(!names.has(action.id) ||
                   !["click","press","change"].includes(action.event) ||
                   !/^[a-zA-Z_][a-zA-Z0-9_.:-]{0,95}$/.test(action.emit) ||
                   (action.animation && !animationsByName.has(action.animation)))
                    throw new TypeError("Invalid declarative UI action");
            }
            // Existing callbacks survive hot reload. Remove only callbacks
            // generated by this XML document, never user-installed callbacks.
            const previous=UI.#assetRoots.get(root);
            if(previous?.boundActions)
                for(const [action,callback] of previous.boundActions)
                    root.off(action.id,action.event,callback);
            if(options.__reuseRoot)
                root.reloadFromAsset(elements);
            else root.batch(editor=>{
                for(const node of elements)
                    editor.add(node.type,node.id,node.props,node.parent);
            });
            const boundActions=[];
            for(const action of actions) {
                const callback=event=>{
                    if(action.animation)UI.playAssetAnimation(root,action.animation);
                    Events.emit(action.emit,{
                        source:"nui",id:action.id,layer,event
                    });
                };
                root.on(action.id,action.event,callback);
                boundActions.push([action,callback]);
            }
            UI.#assetRoots.set(root,{path,source,
                options:{layer,entityId:options.entityId,
                    viewportWidth:options.viewportWidth},
                rejected:null,boundActions,animationsByName,
                activeAnimations:previous?.activeAnimations??[],
                startAnimations:!options.__reuseRoot || previous?.startAnimations,
                width:viewportWidth});
            UI.#startAssetWatch();
            return root;
        }

        static #startAssetWatch() {
            if(UI.#assetWatchEnabled || typeof engine.on!=="function")return;
            UI.#assetWatchEnabled=true;
            Events.on("ui.update",dt=>{
                UI.advanceAnimations(dt);
                UI.#assetWatchElapsed+=Math.min(0.1,Math.max(0,Number(dt)||0));
                if(UI.#assetWatchElapsed<0.5)return;
                UI.#assetWatchElapsed=0;
                const hot=typeof engine.ui.documents.hotReloadEnabled==="function" &&
                    engine.ui.documents.hotReloadEnabled()===true;
                if(hot)UI.reloadChangedAssets();
                // Responsive rules must be reapplied after viewport resize,
                // independent of whether hot reload is enabled.
                if(typeof engine.ui.viewport.viewportSize==="function") {
                    const width=engine.ui.viewport.viewportSize()?.width;
                    if(Number.isFinite(width))for(const [root,meta] of UI.#assetRoots)
                        if(root.mounted && meta.width!==width) {
                            UI.loadAsset(meta.path,{
                                ...meta.options,__reuseRoot:root});
                        }
                }
            });
        }
        /** Released by UIRoot.dispose(), including packaged Player instances. */
        static releaseAssetRoot(root) {
            return UI.#assetRoots.delete(root);
        }
        static playAssetAnimation(root,name) {
            const meta=UI.#assetRoots.get(root);
            const animation=meta?.animationsByName.get(name);
            if(!animation)return false;
            meta.activeAnimations=meta.activeAnimations.filter(a=>a.name!==name);
            meta.activeAnimations.push({...animation,elapsed:0});
            root.setTransient(animation.id,{[animation.property]:animation.from});
            return true;
        }
        /** Deterministic time-step; called on ui.update, available to tests. */
        static advanceAnimations(dt) {
            const elapsed=Math.max(0,Math.min(0.1,Number(dt)||0));
            for(const [root,meta] of UI.#assetRoots) {
                if(!root.mounted)continue;
                if(meta.startAnimations) {
                    meta.startAnimations=false;
                    for(const spec of meta.animationsByName.values())
                        if(spec.trigger==="mount")UI.playAssetAnimation(root,spec.name);
                }
                if(!elapsed || !meta.activeAnimations.length)continue;
                const next=[];
                root.batch(()=>{
                    for(const spec of meta.activeAnimations){
                        spec.elapsed=Math.min(spec.duration,spec.elapsed+elapsed);
                        const t=spec.elapsed/spec.duration;
                        const eased=spec.easing==="easeOutCubic"?1-(1-t)**3:
                            spec.easing==="easeInOut"?t*t*(3-2*t):t;
                        root.setTransient(spec.id,{
                            [spec.property]:spec.from+(spec.to-spec.from)*eased
                        });
                        if(t<1)next.push(spec);
                    }
                });
                meta.activeAnimations=next;
            }
        }
        static reloadChangedAssets() {
            let updated=0;
            for(const [root,meta] of UI.#assetRoots) {
                if(!root.mounted && root.size===0){
                    UI.#assetRoots.delete(root);continue;
                }
                let next;
                try{next=engine.ui.documents.readAsset(meta.path);}
                catch(err){continue;}
                if(next===meta.source || next===meta.rejected)continue;
                try{
                    UI.loadAsset(meta.path,{...meta.options,__reuseRoot:root});
                    updated++;
                }catch(err){
                    meta.rejected=next;
                    try{Logger.warn("UI reload rejected: "+String(err));}catch(_){}
                }
            }
            return updated;
        }
        static hasSurface(entityId) { return UI.#surfaces.has(String(entityId)); }
        static setTitle(text) { return engine.ui.viewport.setTitle(text); }
        /** Whether a named presentation layer is currently registered. */
        static hasLayer(layer) { return UI.#layers.has(layer); }

        static #typed(value) {
            if (typeof value === "number") {
                if (!Number.isFinite(value))
                    throw new TypeError("UI numeric property must be finite");
                return {type:"number",value};
            }
            if (typeof value === "boolean") return {type:"bool",value};
            if (typeof value === "string") {
                if (/^#[0-9a-fA-F]{8}$/.test(value))
                    return {type:"color",value};
                return {type:"text",value};
            }
            throw new TypeError("UI property must be string, boolean or number");
        }

        static #flush() {
            const nodes = [];
            for (const [layer, items] of UI.#layers) {
                for (const node of items) {
                    const props = {};
                    for (const [key, value] of Object.entries(node.props ?? {}))
                        props[key] = UI.#typed(value);
                    if (layer.length + 1 + node.id.length > 96 ||
                        (node.parent && layer.length + 1 + node.parent.length > 96))
                        throw new RangeError("UI scoped node identifier exceeds 96 characters");
                    if (nodes.length >= 256)
                        throw new RangeError("UI document exceeds 256 elements");
                    nodes.push({
                        id: layer + "." + node.id,
                        parent: node.parent ? layer + "." + node.parent : "",
                        type: node.type,
                        props
                    });
                }
            }
            return engine.ui.documents.apply({target:"screen",nodes});
        }

        static mount(layer, elements) {
            if (typeof layer !== "string" ||
                !/^[a-zA-Z][a-zA-Z0-9_-]{0,47}$/.test(layer) ||
                !Array.isArray(elements) || elements.length > 256)
                throw new TypeError("UI.mount requires a layer and bounded element array");
            // Clone user data so later mutation cannot silently desynchronize
            // the native document from its script-owned source.
            const owned = elements.map(node => {
                if (!node || typeof node !== "object" ||
                    !/^[a-zA-Z][a-zA-Z0-9_.-]{0,63}$/.test(node.id) ||
                    (node.parent !== undefined && node.parent !== "" &&
                     !/^[a-zA-Z][a-zA-Z0-9_.-]{0,63}$/.test(node.parent)) ||
                    !["panel","text","button","image","input"].includes(node.type))
                    throw new TypeError("UI node descriptor is invalid");
                return {
                    id: node.id, type:node.type, parent:node.parent ?? "",
                    props:{...node.props}
                };
            });
            // Validate the public authoring tree before it reaches the native
            // provider. Errors point to the actual layer and offending node.
            const known = new Set();
            for (const node of owned) {
                if (known.has(node.id))
                    throw new Error('UI layer "' + layer +
                        '" declares duplicate element "' + node.id + '"');
                if (node.parent && !known.has(node.parent))
                    throw new Error('UI layer "' + layer + '" element "' +
                        node.id + '" must follow its parent "' + node.parent + '"');
                known.add(node.id);
            }
            const previous = UI.#layers.get(layer);
            const unchanged = previous && previous.length === owned.length &&
                previous.every((item,i) => {
                    const next = owned[i];
                    if (item.id !== next.id || item.parent !== next.parent ||
                        item.type !== next.type) return false;
                    const keys = Object.keys(item.props);
                    const nextKeys = Object.keys(next.props);
                    return keys.length === nextKeys.length &&
                        keys.every(key => Object.prototype.hasOwnProperty.call(
                            next.props,key) && Object.is(item.props[key],next.props[key]));
                });
            if (unchanged) return;
            UI.#layers.set(layer, owned);
            try { return UI.#flush(); }
            catch (error) {
                if (previous === undefined) UI.#layers.delete(layer);
                else UI.#layers.set(layer, previous);
                throw error;
            }
        }

        static unmount(layer) {
            if (!UI.#layers.has(layer)) return;
            const previous = UI.#layers.get(layer);
            UI.#layers.delete(layer);
            try { return UI.#flush(); }
            catch (error) {
                UI.#layers.set(layer, previous);
                throw error;
            }
        }

        static mountSurface(entityId, layer, elements, options = {}) {
            const id=String(entityId);
            if (!/^[0-9]{1,32}$/.test(id))
                throw new TypeError("World UI needs a stable Entity ID");
            const width=options.width ?? 512, height=options.height ?? 256;
            const worldWidth=options.worldWidth ?? 1.8;
            const worldHeight=options.worldHeight ?? 1;
            if (!Number.isInteger(width)||!Number.isInteger(height)||
                width<32||height<32||width>1024||height>1024||
                width*height>524288||
                !Number.isFinite(worldWidth)||!Number.isFinite(worldHeight)||
                worldWidth<0.1||worldWidth>20||
                worldHeight<0.1||worldHeight>20)
                throw new RangeError("Invalid world UI dimensions");
            if(typeof layer!=="string"||
               !/^[a-zA-Z][a-zA-Z0-9_-]{0,47}$/.test(layer)||
               !Array.isArray(elements)||elements.length>256)
                throw new TypeError("World UI needs a valid layer and elements");
            const seen=new Set();
            const nodes=elements.map(node=>{
                if(!node||typeof node.id!=="string"||
                   !/^[a-zA-Z][a-zA-Z0-9_.-]{0,63}$/.test(node.id)||
                   !["panel","text","button","image","input"].includes(node.type)||
                   seen.has(node.id)||(node.parent&&!seen.has(node.parent)))
                    throw new TypeError("Invalid world UI element or parent");
                seen.add(node.id);
                const props={};
                for(const [key,value] of Object.entries(node.props??{}))
                    props[key]=UI.#typed(value);
                return {id:layer+"."+node.id,
                    parent:node.parent?layer+"."+node.parent:"",
                    type:node.type,props};
            });
            const data={target:"world",entityId:id,width,height,
                worldWidth,worldHeight,nodes};
            const signature=JSON.stringify(data);
            if(UI.#surfaces.get(id)===signature)return;
            engine.ui.documents.apply(data);
            UI.#surfaces.set(id,signature);
        }
        static unmountSurface(entityId) {
            const id=String(entityId);
            if(!UI.#surfaces.has(id))return;
            engine.ui.documents.apply({target:"world",entityId:id,nodes:[]});
            UI.#surfaces.delete(id);
        }

        static render(elements) { return UI.mount("default", elements); }

        static clear() {
            if (!UI.#layers.size) return;
            const previous = UI.#layers;
            UI.#layers = new Map();
            try { return UI.#flush(); }
            catch(error) {
                UI.#layers = previous;
                throw error;
            }
        }
    }
