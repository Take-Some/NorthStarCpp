    class Player {
        static configure(definition) { return NorthStar.player.configure(definition); }
        static get config() { return NorthStar.player.config; }
        static get enabled() { return NorthStar.player.enabled; }
        static get index() { return engine.player.controller.controlledIndex(); }
        static get entityId() { return engine.player.controller.controlledEntity(); }
        static get hasPawn() { return Player.index >= 0; }
        static requirePawn() {
            const pawn = Scene.findPawn();
            if (!pawn)
                throw new Error("No possessed Pawn. Add a Pawn component and enable Auto Possess Local.");
            return pawn;
        }
        static possess(id) { return engine.player.controller.possess(String(id)); }
        static unpossess() { return engine.player.controller.unpossess(); }
        static sprint(active) { return engine.player.motion.sprint(Boolean(active)); }
        static setIndex(index) { return NorthStar.player.setIndex(index); }
        static get paused() { return NorthStar.player.paused; }
        static setPaused(value) { return NorthStar.player.setPaused(value); }
        static move(x, z) { return engine.player.motion.move(x, z); }
        static motion() { return engine.player.motion.motion(); }
        /** Provider-independent post-physics state, or null before first tick. */
        static motionState() { return engine.player.motion.motionState(); }
        /** Subscribe to player.jump / player.fall / player.land / move events. */
        static on(action, handler) {
            if(typeof action!=="string" ||
               !/^[a-zA-Z][a-zA-Z0-9_.]{0,63}$/.test(action))
                throw new TypeError("Invalid Player action name");
            return Events.on("player."+action,handler);
        }
        static once(action, handler) {
            if(typeof action!=="string" ||
               !/^[a-zA-Z][a-zA-Z0-9_.]{0,63}$/.test(action))
                throw new TypeError("Invalid Player action name");
            return Events.once("player."+action,handler);
        }
        static ground() { return engine.player.motion.ground(); }
        static jump() { return engine.player.motion.jump(); }
        static eye() { return engine.player.motion.eye(); }
        static setEyeHeight(height) { return engine.player.motion.setEyeHeight(height); }
    }

    // Base lifecycle for independent root-script classes. A root class
    // registers once and keeps its mutable state in private instance fields.
