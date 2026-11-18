    // One resource facade: delegate to authoritative scene/UI asset paths.
    // Generic asset streaming requires the native engine.assets capability.
    class Assets {
        static available(kind) {
            if(kind==="prefab"||kind==="ui")return true;
            return typeof engine.assets?.files?.open==="function";
        }
        static prefab(path,options={}) {
            return Factory.prefab(path,options);
        }
        static ui(path,options={}) {
            return UI.loadAsset(Contracts.vfs(path,".nui","ui/"),
                Contracts.options(options,"UI asset options"));
        }
        static open(path,kind,options={}) {
            Contracts.vfs(path,"","");
            if(typeof kind!=="string" || !kind)
                throw new TypeError("Asset kind is required");
            if(kind==="prefab")return Assets.prefab(path,options);
            if(kind==="ui")return Assets.ui(path,options);
            if(!Assets.available(kind))
                throw new EngineCapabilityError("assets.open:"+kind);
            return engine.assets.files.open(kind,path,
                Contracts.options(options,"Asset load options"));
        }
    }
