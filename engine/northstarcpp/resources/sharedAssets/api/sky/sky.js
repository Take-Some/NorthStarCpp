    // Render Sky facade. The world owns sky state; the renderer consumes its snapshot.
    // State and simulation live in the native engine.render.sky provider; all GPU
    // backends consume one snapshot. Never create a duplicate JS clock.
    class Sky {
        static #native(method) {
            const fn=engine.render.sky?.[method];
            if(typeof fn!=="function")
                throw new EngineCapabilityError("engine.render.sky."+method);
            return fn;
        }
        static configure(settings={}) {
            if(!settings || typeof settings!=="object" || Array.isArray(settings))
                throw new TypeError("Sky.configure requires a settings object");
            const booleans=new Set(["enabled","paused","sun","moon","stars","clouds"]);
            const ranges={
                timeOfDay:[0,24],
                dayLengthSeconds:[30,86400],
                latitudeDegrees:[-90,90],
                dayOfYear:[1,365],
                moonPhase:[0,1],
                cloudCoverage:[0,1],
                cloudSpeed:[0,1],
                haze:[0,1],
                fogDensity:[0,.25],
                sunIntensity:[0,8],
                moonIntensity:[0,4],
                starDensity:[0,1]
            };
            for(const [name,value] of Object.entries(settings)) {
                if(booleans.has(name)) {
                    if(typeof value!=="boolean")
                        throw new TypeError("Sky."+name+" must be boolean");
                    continue;
                }
                if(name==="mode") {
                    if(!["procedural","drawable","hybrid","disabled"].includes(value))
                        throw new RangeError("Invalid Sky mode");
                    continue;
                }
                if(name==="drawable" || name==="preset") {
                    if(typeof value!=="string" || value.length>512)
                        throw new TypeError("Sky."+name+" must be a string");
                    continue;
                }
                const range=ranges[name];
                if(!range)throw new RangeError("Unknown Sky property: "+name);
                if(typeof value!=="number" || !Number.isFinite(value) ||
                   value<range[0] || value>range[1] ||
                   (name==="timeOfDay" && value===24) ||
                   (name==="dayOfYear" && !Number.isInteger(value)))
                    throw new RangeError("Sky."+name+" out of range");
            }
            Sky.#native("configure")(JSON.stringify(settings));
            return Sky.state();
        }
        static state() {
            const source=Sky.#native("state")();
            if(!source || typeof source!=="object" ||
               !source.sunDirection || !source.moonDirection)
                throw new Error("engine.render.sky returned invalid state");
            return Object.freeze({
                ...source,
                sunDirection:Object.freeze({...source.sunDirection}),
                moonDirection:Object.freeze({...source.moonDirection})
            });
        }
        static enable(enabled=true) {
            if(typeof enabled!=="boolean")
                throw new TypeError("Sky.enable expects boolean");
            return Sky.configure({enabled});
        }
        static pause(paused=true) {
            if(typeof paused!=="boolean")
                throw new TypeError("Sky.pause expects boolean");
            return Sky.configure({paused});
        }
        static setTimeOfDay(hours,minutes=0) {
            if(!Number.isInteger(hours)||hours<0||hours>23 ||
               !Number.isInteger(minutes)||minutes<0||minutes>59)
                throw new RangeError("Sky time must be 00:00..23:59");
            // Explicitly opting into celestial time also enables the sky.
            return Sky.configure({enabled:true,timeOfDay:hours+minutes/60});
        }
        static setDayLength(seconds) {
            return Sky.configure({enabled:true,dayLengthSeconds:seconds});
        }
        static setCloudCoverage(coverage) {
            return Sky.configure({enabled:true,clouds:true,cloudCoverage:coverage});
        }
        static setClouds(settings={}) {
            if(typeof settings==="number")
                settings={coverage:settings};
            if(!settings || typeof settings!=="object"||Array.isArray(settings))
                throw new TypeError("Sky.setClouds expects coverage or object");
            const accepted=new Set(["enabled","coverage","speed"]);
            for(const key of Object.keys(settings))
                if(!accepted.has(key))
                    throw new RangeError("Unknown Sky cloud property: "+key);
            const next={enabled:true};
            if(settings.enabled!==undefined)next.clouds=settings.enabled;
            else next.clouds=true;
            if(settings.coverage!==undefined)
                next.cloudCoverage=settings.coverage;
            if(settings.speed!==undefined)
                next.cloudSpeed=settings.speed;
            return Sky.configure(next);
        }
        static setSun(settings={}) {
            if(typeof settings==="boolean")settings={enabled:settings};
            if(!settings || typeof settings!=="object"||Array.isArray(settings))
                throw new TypeError("Sky.setSun expects boolean or object");
            for(const key of Object.keys(settings))
                if(key!=="enabled"&&key!=="intensity")
                    throw new RangeError("Unknown Sky sun property: "+key);
            const next={enabled:true,sun:settings.enabled??true};
            if(settings.intensity!==undefined)next.sunIntensity=settings.intensity;
            return Sky.configure(next);
        }
        static setMoon(settings={}) {
            if(typeof settings==="boolean")settings={enabled:settings};
            if(!settings||typeof settings!=="object"||Array.isArray(settings))
                throw new TypeError("Sky.setMoon expects boolean or object");
            for(const key of Object.keys(settings))
                if(!["enabled","intensity","phase"].includes(key))
                    throw new RangeError("Unknown Sky moon property: "+key);
            const next={enabled:true,moon:settings.enabled??true};
            if(settings.intensity!==undefined)next.moonIntensity=settings.intensity;
            if(settings.phase!==undefined)next.moonPhase=settings.phase;
            return Sky.configure(next);
        }
        static setStars(settings={}) {
            if(typeof settings==="boolean")settings={enabled:settings};
            if(!settings||typeof settings!=="object"||Array.isArray(settings))
                throw new TypeError("Sky.setStars expects boolean or object");
            for(const key of Object.keys(settings))
                if(key!=="enabled"&&key!=="density")
                    throw new RangeError("Unknown Sky stars property: "+key);
            const next={enabled:true,stars:settings.enabled??true};
            if(settings.density!==undefined)next.starDensity=settings.density;
            return Sky.configure(next);
        }
        // Explicit deterministic stepping is intended for scripted events or
        // tests; ordinary frame time is advanced by the engine.
        static advance(seconds) {
            if(typeof seconds!=="number" || !Number.isFinite(seconds) ||
               seconds<0||seconds>1)
                throw new RangeError("Sky.advance must be 0..1 seconds");
            Sky.#native("advance")(seconds);
            return Sky.state();
        }
        static reset() {
            Sky.#native("reset")();
            return Sky.state();
        }
        static get timeOfDay(){return Sky.state().timeOfDay;}
        static get sunDirection(){return Sky.state().sunDirection;}
        static get moonDirection(){return Sky.state().moonDirection;}
        static get daylight(){return Sky.state().daylight;}
    }
