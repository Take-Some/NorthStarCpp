    class Camera {
        static available() { return engine.camera.views.available(); }
        static mode() { return engine.camera.views.mode(); }
        static setMode(mode, seconds = 0.45) {
            return engine.camera.views.setMode(mode, seconds);
        }
        static angles() { return engine.camera.views.angles(); }
        static setAngles(yaw, pitch) { return engine.camera.views.setAngles(yaw, pitch); }
        static followDistance() { return engine.camera.views.followDistance(); }
        static setFollowDistance(distance) {
            return engine.camera.views.setFollowDistance(distance);
        }
        static setZoom(amount) { return engine.camera.views.setZoom(amount); }
        static shake(amplitudeMetres, seconds) {
            return engine.camera.effects.shake(amplitudeMetres, seconds);
        }
        static recoil(pitchRadians,yawRadians=0,seconds=.24) {
            return engine.camera.effects.recoil(pitchRadians,yawRadians,seconds);
        }
        static cut() { return engine.camera.shots.cut(); }
        /** Highest priority request wins; ties go to the latest source. */
        static pushShot(source, pose, priority = 0, blendSeconds = 0.35) {
            Contracts.identifier(source, "Camera shot source");
            if (!pose || typeof pose !== "object" || !pose.position)
                throw new TypeError("Camera shot requires a world-space pose");
            const p = pose.position;
            const values = [
                p.x, p.y, p.z, pose.yaw, pose.pitch,
                pose.roll === undefined ? 0 : pose.roll,
                pose.fovY === undefined ? 75 : pose.fovY,
                priority, blendSeconds
            ];
            if (!values.every(Number.isFinite) ||
                !Number.isInteger(priority) || priority < -100 ||
                priority > 100 || blendSeconds < 0 || blendSeconds > 10)
                throw new RangeError("Invalid camera shot transform, priority or blend");
            return engine.camera.shots.pushShot(source, ...values);
        }
        static clearShot(source) {
            Contracts.identifier(source, "Camera shot source");
            return engine.camera.shots.clearShot(source);
        }
        static clearShots() { return engine.camera.shots.clearShots(); }
        static activeShot() { return engine.camera.shots.activeShot(); }
        /** Read a named preset from project VFS gameplay/camera_presets.json. */
        static applyPreset(id) {
            Contracts.identifier(id, "Camera preset identifier");
            return engine.camera.shots.applyPreset(id);
        }
        /** Drive a camera by world-space point-of-interest, without project math. */
        static lookAt(source, position, target, {fovY = 75,
                           priority = 0, blendSeconds = 0.35} = {}) {
            Contracts.vector(position,"Camera position");
            Contracts.vector(target,"Camera target");
            const dx = target.x-position.x, dy = target.y-position.y,
                  dz = target.z-position.z;
            if (Math.hypot(dx,dy,dz) < 1e-6)
                throw new RangeError("Camera look-at target coincides with eye");
            if (!Number.isFinite(fovY) || fovY <= 1 || fovY >= 179)
                throw new RangeError("Camera field of view must be 1..179 degrees");
            return Camera.pushShot(source, {
                position, yaw: Math.atan2(dx,dz),
                pitch: Math.atan2(dy,Math.hypot(dx,dz)), fovY
            }, priority, blendSeconds);
        }
        static #asset(path) {
            return Contracts.vfs(path,"");
        }
        static playTrack(path, loop = false, blendSeconds = 0.5) {
            Camera.#asset(path);
            if (typeof loop !== "boolean" || !Number.isFinite(blendSeconds) ||
                blendSeconds < 0 || blendSeconds > 10)
                throw new RangeError("Invalid camera track options");
            return engine.camera.cinematics.playTrack(path,loop,blendSeconds);
        }
        static playCutscene(path, {loop = false,
                entrySeconds = 0.55,exitSeconds = 0.55} = {}) {
            Camera.#asset(path);
            if (typeof loop !== "boolean" ||
                ![entrySeconds,exitSeconds].every(n =>
                    Number.isFinite(n) && n >= 0 && n <= 10))
                throw new RangeError("Invalid camera cutscene transition");
            return engine.camera.cinematics.playCutscene(path,loop,entrySeconds,exitSeconds);
        }
        static stopCutscene(exitSeconds = 0.55) {
            if (!Number.isFinite(exitSeconds) || exitSeconds < 0 || exitSeconds > 10)
                throw new RangeError("Invalid cutscene exit time");
            return engine.camera.cinematics.stopCutscene(exitSeconds);
        }
        static pauseCutscene(paused = true) {
            if (typeof paused !== "boolean") throw new TypeError("Expected pause boolean");
            return engine.camera.cinematics.pauseCutscene(paused);
        }
        static seekCutscene(seconds) {
            if (!Number.isFinite(seconds) || seconds < 0)
                throw new RangeError("Cutscene seek time must be nonnegative");
            return engine.camera.cinematics.seekCutscene(seconds);
        }
        static cutsceneState() { return engine.camera.cinematics.cutsceneState(); }
        static lensState() { return engine.camera.cinematics.lensState(); }

        static createViewController(options = {}) {
            return new CameraViewController(options);
        }
    }

    // Key enumerations are self-contained classes loaded from scripts/input/.
    // Register once per V8 context before other SDK/gameplay scripts execute.
