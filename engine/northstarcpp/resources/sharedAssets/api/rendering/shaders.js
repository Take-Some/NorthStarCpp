    // Engine Render is the root gateway for built-in and project shader assets.
    // Source authority is native engine.render; no separate JS compiler or
    // renderer-specific duplicate shader registry is created.
    class RenderShaders {
        static #native(method) {
            const nested={shaderCatalog:"catalog",shaderSource:"source",
                          shaderQueueCompiled:"queueCompiled",shaderPoll:"poll"};
            const fn=engine.render?.shaders?.[nested[method]];
            if(typeof fn!=="function")
                throw new EngineCapabilityError("engine.render.shaders."+nested[method]);
            return fn;
        }
        static #path(value,label="Shader path") {
            if(typeof value!=="string" ||
               value.length<12 || value.length>256 ||
               !value.startsWith("shaders/") ||
               !/^[a-zA-Z0-9_./-]+$/.test(value) ||
               value.includes("..") || value.includes("//") ||
               !/\.(glsl|hlsl|vert|frag|comp|geom|tesc|tese)$/.test(value))
                throw new TypeError(label+" must be a safe shaders/* source path");
            return value;
        }
        static #binary(value) {
            if(typeof value!=="string" || value.length>256 ||
               !value.startsWith("shaders/") ||
               !/^[a-zA-Z0-9_./-]+$/.test(value) ||
               value.includes("..") || value.includes("//") ||
               !/\.(spv|dxil)$/.test(value))
                throw new TypeError("Compiled shader must be shaders/*.spv or shaders/*.dxil");
            return value;
        }
        static catalog() {
            const catalog=RenderShaders.#native("shaderCatalog")();
            if(!catalog || !Array.isArray(catalog.builtin))
                throw new Error("engine.render returned malformed shader catalog");
            const builtin=Object.freeze(catalog.builtin.map(record=>
                Object.freeze({...record})));
            return Object.freeze({...catalog,builtin});
        }
        static source(path) {
            return RenderShaders.#native("shaderSource")(RenderShaders.#path(path));
        }
        static load(path) {
            const name=RenderShaders.#path(path);
            const source=RenderShaders.source(name);
            const language=name.endsWith(".hlsl")?"hlsl":"glsl";
            return Object.freeze({path:name,language,source});
        }
        static queueCompiled(sourcePath,binaryPath) {
            const source=RenderShaders.#path(sourcePath);
            const binary=RenderShaders.#binary(binaryPath);
            if((source.endsWith(".hlsl") && !binary.endsWith(".dxil")) ||
               (!source.endsWith(".hlsl") && !binary.endsWith(".spv")))
                throw new TypeError("Compiled shader format does not match source language");
            // Native hot reload queues an existing compiled binary for the
            // matching active material. It does not compile source code.
            return RenderShaders.#native("shaderQueueCompiled")(source,binary);
        }
        static poll() {
            const status=RenderShaders.#native("shaderPoll")();
            if(!status || typeof status.pending!=="boolean" ||
               !Array.isArray(status.completed))
                throw new Error("engine.render returned malformed shader status");
            return Object.freeze({
                pending:status.pending,
                completed:Object.freeze([...status.completed])
            });
        }
        static get supportsHotReload() {
            return RenderShaders.catalog().hotReload;
        }
    }

    // Canonical render API: grouped, provider-neutral services only.
    class Render {
        static get shaders(){return RenderShaders;}
        static get shadows(){return RenderShadows;}
        // A rendering facade for the one world-owned engine.render.sky state.
        static get sky(){return Sky;}
        static get provider(){return RenderProvider;}
        static version(){return RenderProvider.version();}
        static setDetailLevel(level){return RenderProvider.setDetailLevel(level);}
        static detailLevel(){return RenderProvider.detailLevel();}
    }
