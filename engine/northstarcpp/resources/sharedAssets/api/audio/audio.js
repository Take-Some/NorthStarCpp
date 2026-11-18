    // Single provider-neutral playback contract. No gameplay event routing,
    // no hardcoded asset names and no independent sound implementation.
    class AudioVoice {
        #id;
        #active=true;
        constructor(id) {
            if(!Number.isSafeInteger(id) || id<=0)
                throw new RangeError("Native audio voice requires positive ID");
            this.#id=id;
            Object.freeze(this);
        }
        get id() { return this.#id; }
        get active() { return this.#active; }
        stop() {
            if(!this.#active)return false;
            if(typeof engine.audio?.voices?.stopVoice!=="function")
                throw new Error("Native voice stop capability unavailable");
            engine.audio.voices.stopVoice(this.#id);
            this.#active=false;
            return true;
        }
        setGain(gain) {
            if(!this.#active)throw new Error("Audio voice already stopped");
            Contracts.number(gain,"Voice gain",0,2);
            if(typeof engine.audio?.voices?.setVoiceGain!=="function")
                throw new Error("Native voice gain capability unavailable");
            engine.audio.voices.setVoiceGain(this.#id,gain);
            return this;
        }
        setPosition(position) {
            if(!this.#active)throw new Error("Audio voice already stopped");
            Contracts.vector(position,"Voice position");
            if(typeof engine.audio?.spatial?.setVoicePosition!=="function")
                throw new Error("Native voice spatial capability unavailable");
            engine.audio.spatial.setVoicePosition(this.#id,
                position.x,position.y,position.z);
            return this;
        }
        dispose() { return this.stop(); }
    }

    class Audio {
        static supportsVoices() {
            return typeof engine.audio?.voices?.supportsVoices==="function" &&
                engine.audio.voices.supportsVoices()===true;
        }
        static playVoice(request, options={}) {
            if(!Audio.supportsVoices())
                throw new Error("Native ABI-2 audio voice provider unavailable");
            const descriptor=typeof request==="string"?
                {asset:request,...Contracts.options(options,"Audio options")} :
                Contracts.options(request,"Audio request");
            if(typeof descriptor.asset!=="string" ||
               !descriptor.asset.endsWith(".nac"))
                throw new TypeError("Managed voices require a NAC audio asset");
            const id=Audio.play(descriptor);
            return new AudioVoice(id);
        }

        static play(request, options = {}) {
            const data = typeof request === "string" ?
                {asset:request,...Contracts.options(options,"Audio options")} :
                Contracts.options(request,"Audio request");
            for(const key of Object.keys(data)) {
                if(!["asset","clip","variation","gain","position"].includes(key))
                    throw new RangeError("Unsupported Audio.play option: "+key);
            }
            const {asset,clip=null,variation=0,gain=1,position=null}=data;
            Contracts.vfs(asset,"","audio/");
            if(asset.length>256)
                throw new RangeError("Audio virtual path exceeds provider limit");
            Contracts.number(gain,"Audio gain",0,2);
            if(!Number.isSafeInteger(variation) || variation<0 || variation>1000000)
                throw new RangeError("Audio variation must be 0..1000000");
            if(asset.endsWith(".nac")) {
                if(typeof clip!=="string" || !/^[a-zA-Z0-9_]{1,48}$/.test(clip))
                    throw new TypeError("NAC playback requires valid clip identifier");
                if (position !== null) {
                    Contracts.vector(position,"Audio position");
                    // The host implements spatial NAC through the optional
                    // 7-argument voice ABI. Never invent WAV spatialization.
                    return engine.audio.playback.playNac(asset,clip,variation,gain,
                        position.x,position.y,position.z);
                }
                return engine.audio.playback.playNac(asset,clip,variation,gain);
            }
            if(asset.endsWith(".wav")) {
                if(clip!==null || variation!==0 || gain!==1 || position!==null)
                    throw new RangeError(
                        "Current WAV provider supports no clip, variation or per-voice gain");
                return engine.audio.playback.play(asset);
            }
            throw new TypeError("Audio supports WAV and NAC assets");
        }
        /** Compatibility aliases, delegated to the same single contract. */
        static playWav(path) { return Audio.play(path); }
        static playNac(path,clip,variation=0,gain=1) {
            return Audio.play({asset:path,clip,variation,gain});
        }
        static playNacAt(path,clip,position,variation=0,gain=1) {
            return Audio.play({asset:path,clip,position,variation,gain});
        }
    }
