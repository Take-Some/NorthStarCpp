// NorthStarCpp shared Character.js: project-agnostic OOP character controller.
// The project provides asset VFS paths and animation names; the renderer
// evaluates the requested skeleton and cannot assume any particular avatar.
(() => {
    "use strict";

    if (typeof NorthStar !== "object" || !NorthStar.character ||
        typeof NorthStar.character.registerController !== "function")
        throw new Error("Character.js requires the NorthStarSDK bootstrap");

    class CharacterController {
        #resources;
        #states;
        #animationNames;
        #tuning;
        #hideInFirstPerson;
        #firstPersonArms;
        #selectAnimation;
        #initialHeading;
        #entityIndex = -1;
        #previous = null;
        #heading;
        #animation;
        #elapsed = 0;
        #active = false;

        constructor(options = {}) {
            const resources = options.resources;
            if (!resources || typeof resources !== "object" ||
                !Array.isArray(resources.animations) || resources.animations.length === 0)
                throw new TypeError("Character controller requires model/skeleton/animations resources");
            for (const key of ["model", "skeleton", "material", "textures"]) {
                if (typeof resources[key] !== "string" || !resources[key])
                    throw new TypeError("Missing character resource: " + key);
            }
            const animations = resources.animations.map(clip => {
                if (!clip || typeof clip.name !== "string" || !clip.name ||
                    typeof clip.source !== "string" || !clip.source ||
                    typeof clip.clip !== "string" || !clip.clip)
                    throw new TypeError("Invalid character animation descriptor");
                return Object.freeze({name: clip.name, source: clip.source, clip: clip.clip});
            });
            this.#resources = Object.freeze({
                model: resources.model,
                skeleton: resources.skeleton,
                material: resources.material,
                textures: resources.textures,
                animations: Object.freeze(animations)
            });

            this.#states = Object.freeze(Object.assign({
                idle: "idle", walking: "walk", jumping: "jump", falling: "fall"
            }, options.states || {}));
            const names = new Set(animations.map(clip => clip.name));
            if (names.size !== animations.length)
                throw new RangeError("Duplicate character animation names");
            this.#animationNames = names;
            for (const name of Object.values(this.#states)) {
                if (typeof name !== "string" || !names.has(name))
                    throw new RangeError("Undefined character animation state: " + name);
            }

            this.#tuning = Object.freeze(Object.assign({
                walkSpeed: 0.15, teleportSpeed: 35,
                airborneSpeed: 0.35, headingSeconds: 0.12, jumpHoldSeconds: 0.6
            }, options.tuning || {}));
            for (const [key, value] of Object.entries(this.#tuning)) {
                if (!Number.isFinite(value) || value < 0 ||
                    (key === "teleportSpeed" && value === 0))
                    throw new RangeError("Invalid character movement tuning: " + key);
            }
            if (options.selectAnimation !== undefined &&
                typeof options.selectAnimation !== "function")
                throw new TypeError("selectAnimation must be a function");
            this.#selectAnimation = options.selectAnimation || null;
            this.#hideInFirstPerson = options.hideInFirstPerson !== false;
            if(options.firstPersonArms!==undefined &&
               typeof options.firstPersonArms!=="boolean")
                throw new TypeError("firstPersonArms must be boolean");
            this.#firstPersonArms=options.firstPersonArms===true;
            this.#initialHeading = options.initialHeading === undefined
                ? 0 : options.initialHeading;
            if (!Number.isFinite(this.#initialHeading))
                throw new RangeError("Invalid initial character heading");

            this.#heading = this.#initialHeading;
            this.#animation = this.#states.idle;
            Object.freeze(this);
        }

        #sample(dt) {
            const actor = engine.scene.objects(this.#entityIndex, 1)[0];
            if (!actor) throw new Error("Character scene entity no longer exists");
            const position = actor.position;
            let moving = false;
            let airborne = false;
            let speed = 0;

            if (this.#previous && dt > 0) {
                const dx = position.x - this.#previous.x;
                const dz = position.z - this.#previous.z;
                speed = Math.hypot(dx, dz) / dt;
                // Teleports and large physics corrections do not count as walking.
                if (Number.isFinite(speed) && speed <= this.#tuning.teleportSpeed) {
                    moving = speed > this.#tuning.walkSpeed;
                    airborne = Math.abs(position.y - this.#previous.y) >
                        dt * this.#tuning.airborneSpeed;
                    if (moving) {
                        const wanted = Math.atan2(dx, dz);
                        const delta = Math.atan2(Math.sin(wanted - this.#heading),
                            Math.cos(wanted - this.#heading));
                        const factor = this.#tuning.headingSeconds > 0
                            ? 1 - Math.exp(-dt / this.#tuning.headingSeconds) : 1;
                        this.#heading += delta * factor;
                    }
                }
            }
            this.#previous = {x: position.x, y: position.y, z: position.z};

            let next = airborne
                ? ((this.#animation === this.#states.jumping &&
                    this.#elapsed < this.#tuning.jumpHoldSeconds) ||
                   (this.#animation !== this.#states.jumping &&
                    this.#animation !== this.#states.falling)
                    ? this.#states.jumping : this.#states.falling)
                : moving ? this.#states.walking : this.#states.idle;

            if (this.#selectAnimation) {
                next = this.#selectAnimation({
                    dt, entityIndex: this.#entityIndex, position,
                    heading: this.#heading, speed, moving, airborne,
                    animation: this.#animation, elapsed: this.#elapsed,
                    suggested: next
                });
            }
            if (typeof next !== "string" ||
                !this.#animationNames.has(next))
                throw new RangeError("Undeclared project animation: " + next);
            if (next !== this.#animation) {
                this.#animation = next;
                this.#elapsed = 0;
            }
            this.#elapsed += dt;

            if (this.#hideInFirstPerson && engine.camera.available() &&
                engine.camera.mode() === "first_person") {
                if(this.#firstPersonArms)
                    engine.character.pose(this.#entityIndex,this.#heading,
                        this.#animation,true);
                else engine.character.hide();
            } else {
                engine.character.pose(this.#entityIndex, this.#heading, this.#animation);
            }
        }

        start(index) {
            if (this.#active)
                throw new Error("Character controller is already started");
            if (!Number.isSafeInteger(index) || index < 0)
                throw new RangeError("Character scene index must be a nonnegative integer");
            if (!engine.scene.objects(index, 1)[0])
                throw new RangeError("Character scene entity does not exist");

            engine.character.load(this.#resources);
            this.#entityIndex = index;
            this.#active = true;
            this.#previous = null;
            this.#heading = this.#initialHeading;
            this.#animation = this.#states.idle;
            this.#elapsed = 0;
            try { this.#sample(0); }
            catch (error) {
                this.stop();
                throw error;
            }
        }

        update(dt) {
            if (!this.#active) return;
            if (!Number.isFinite(dt) || dt < 0 || dt > 0.25)
                throw new RangeError("Character update dt must be 0..0.25 seconds");
            this.#sample(dt);
        }

        stop() {
            if (!this.#active) return;
            try { engine.character.clear(); }
            finally {
                this.#active = false;
                this.#entityIndex = -1;
                this.#previous = null;
            }
        }

        state() {
            return Object.freeze({
                active: this.#active,
                entityIndex: this.#entityIndex,
                animation: this.#animation,
                heading: this.#heading,
                elapsed: this.#elapsed
            });
        }
    }

    NorthStar.character.registerController(CharacterController);
})();
