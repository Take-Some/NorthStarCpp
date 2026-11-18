    class Particles {
        static available(effect = "weapon.muzzle") {
            return effect === "weapon.muzzle"
                ? typeof engine.weaponfx?.effects?.emitShot === "function"
                : typeof engine.particles?.emit === "function";
        }
        static emit(effect, {position, direction = {x:0,y:1,z:0}, scale = 1} = {}) {
            if (typeof effect !== "string" ||
                !/^[a-zA-Z0-9_.:-]{1,96}$/.test(effect))
                throw new TypeError("Particle effect identifier is invalid");
            Contracts.vector(position,"Particle position");
            Contracts.vector(direction,"Particle direction");
            Contracts.number(scale,"Particle scale",Number.MIN_VALUE,100);
            const len = Math.hypot(direction.x,direction.y,direction.z);
            if (len < 1e-6) throw new RangeError("Particle direction must be nonzero");
            const dir = {x:direction.x/len,y:direction.y/len,z:direction.z/len};
            if (effect === "weapon.muzzle") {
                if (scale !== 1)
                    throw new RangeError("Native muzzle effect has no scale support");
                if (!Particles.available(effect))
                    throw new Error("Native muzzle effect provider unavailable");
                return Boolean(engine.weaponfx.effects.emitShot(position.x,position.y,position.z,
                    dir.x,dir.y,dir.z));
            }
            if (!Particles.available(effect))
                throw new Error("Generic particle provider unavailable: "+effect);
            // Optional provider ABI: emit({effect,position,direction,scale}).
            return Boolean(engine.particles.emit({effect,position,direction:dir,scale}));
        }
        static tryEmit(effect, options = {}) {
            if (!Particles.available(effect)) return false;
            return Particles.emit(effect, options);
        }
    }
