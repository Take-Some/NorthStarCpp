    class Character {
        static createController(options = {}) {
            return NorthStar.character.createController(options);
        }
        static get Controller() { return NorthStar.character.Controller; }
    }

    // Non-owning authored-entity handle. Keep stable identity, not just
    // an array slot; an index can be reused by a different ECS entity.
    class AuthoredEntityHandle {
        #id;
        #index;
        constructor(record) {
            if (!record || typeof record.id !== "string" ||
                !Number.isSafeInteger(record.index) || record.index < 0)
                throw new TypeError("Invalid authored entity record");
            this.#id = record.id;
            this.#index = record.index;
            Object.freeze(this);
        }
        get id() { return this.#id; }
        get index() { return this.#index; }
        get alive() {
            return Scene.get(this.#index)?.id === this.#id;
        }
        get snapshot() {
            const record = Scene.get(this.#index);
            return record?.id === this.#id ? record : null;
        }
        get position() { return this.snapshot?.position ?? null; }
        getComponent(name) {
            this.#requireAlive();
            return ECS.get(this,name);
        }
        hasComponent(name) {
            return this.alive && ECS.has(this,name);
        }
        setComponent(name,value) {
            this.#requireAlive();
            ECS.set(this,name,value);
            return this;
        }

        #requireAlive() {
            if (!this.alive)
                throw new Error("Authored entity handle is stale: " + this.#id);
        }
        setPosition(value) {
            this.#requireAlive();
            Scene.moveStatic(this.#index,value);
            return this;
        }
        setRotation(value) {
            this.#requireAlive();
            Scene.rotateStatic(this.#index,value);
            return this;
        }
    }

    // Clear, minimal facades over the replaceable host capability namespaces.
    class Scene {
        // Allocated once per V8 isolate, not on every entity spawn.
        static #spawnOptionNames = new Set([
            "name", "position", "size", "mass", "color", "asset", "layer",
            "parent", "collision", "scale"
        ]);
        // Page size matches the native engine.scene.entities.objects maximum (512).
        static objects(start = 0, count = 256) {
            if (!Number.isSafeInteger(start) || start < 0 ||
                !Number.isSafeInteger(count) || count < 1 || count > 512)
                throw new RangeError("Scene page requires start >= 0 and count 1..512");
            return engine.scene.entities.objects(start, count);
        }

        static objectCount() { return engine.scene.entities.objectCount(); }

        static spawn(options = {}) {
            if (!options || typeof options !== "object" || Array.isArray(options))
                throw new TypeError("Scene.spawn requires an entity descriptor");
            for (const name of Object.keys(options)) {
                if (!Scene.#spawnOptionNames.has(name))
                    throw new RangeError("Unsupported Scene.spawn option: " + name);
            }
            const {name = "Runtime Entity", position, size = 1,
                mass = 0, color = 0x46b9d8, asset = "", layer = "Default",
                parent = null, collision = true,
                scale = {x:1,y:1,z:1}} = options;
            if (typeof name !== "string" || !name ||
                typeof layer !== "string" || !layer ||
                typeof asset !== "string" ||
                !position || ![position.x, position.y, position.z].every(Number.isFinite) ||
                !Number.isFinite(size) || size <= 0 || size > 1000 ||
                !Number.isFinite(mass) || mass < 0 || mass > 1000000 ||
                typeof collision !== "boolean" ||
                !scale || ![scale.x,scale.y,scale.z].every(
                    value=>Number.isFinite(value) && value>0 && value<=100) ||
                !Number.isInteger(color) || color < 0 || color > 0xffffff)
                throw new TypeError("Invalid Scene.spawn entity descriptor");
            let parentId = "0";
            if (parent !== null) {
                if (parent instanceof NorthStar.sceneTypes.Entity) {
                    if (!parent.alive)
                        throw new Error("Cannot parent to a destroyed entity");
                    parentId = parent.id;
                } else if (typeof parent === "string" && /^[0-9]+$/.test(parent)) {
                    parentId = parent;
                } else {
                    throw new TypeError("Scene.spawn parent must be an Entity or stable ID");
                }
            }
            const args = [name, position.x, position.y, position.z, size, mass,
                color, asset, layer, parentId];
            if(scale.x!==1 || scale.y!==1 || scale.z!==1) {
                // Visual mesh scale independent of the physics proxy size.
                args.push(collision,scale.x,scale.y,scale.z);
            } else if(!collision)args.push(false);
            const record = engine.scene.entities.spawn(...args);
            return new NorthStar.sceneTypes.Entity(record);
        }

        static get(index) {
            if (!Number.isSafeInteger(index) || index < 0)
                throw new RangeError("Scene object index must be nonnegative");
            return Scene.objects(index, 1)[0] || null;
        }

        static *iterate(pageSize = 256) {
            if (!Number.isSafeInteger(pageSize) || pageSize < 1 || pageSize > 512)
                throw new RangeError("Scene page size must be 1..512");
            for (let start = 0; ; start += pageSize) {
                const page = Scene.objects(start, pageSize);
                for (const object of page) if (object) yield object;
                if (page.length < pageSize) return;
            }
        }

        static findByName(name) {
            if (typeof name !== "string" || !name)
                throw new TypeError("Scene object name must be a nonempty string");
            for (const object of Scene.iterate()) {
                if (object.name === name) return object;
            }
            return null;
        }

        /** Use for required authored scene dependencies with useful errors. */
        static requireByName(name) {
            const object = Scene.findByName(name);
            if (!object) throw new Error('Scene entity "' + name + '" was not found');
            return object;
        }

        /** Handle to an already-authored entity, without instancing a prefab. */
        static handleByName(name) {
            const record = Scene.findByName(name);
            return record ? new AuthoredEntityHandle(record) : null;
        }
        static handle(index) {
            const record = Scene.get(Contracts.index(index,"Entity index"));
            return record ? new AuthoredEntityHandle(record) : null;
        }

        /** Strong alternative to Scene.get() for required entity indices. */
        static require(index) {
            const object = Scene.get(index);
            if (!object) throw new Error("Scene entity index " + index + " does not exist");
            return object;
        }

        // Ordered by scene index, not proximity. maxResults is an explicit
        // query budget to avoid unbounded allocations on large maps.
        static nearby(center, radius, maxResults = 256) {
            if (!center || ![center.x, center.y, center.z].every(Number.isFinite) ||
                !Number.isFinite(radius) || radius < 0 ||
                !Number.isSafeInteger(maxResults) || maxResults < 1 ||
                maxResults > 16384)
                throw new RangeError("Invalid scene sphere query");
            const result = [];
            const squared = radius * radius;
            for (const object of Scene.iterate()) {
                const p = object.position;
                const dx = p.x - center.x;
                const dy = p.y - center.y;
                const dz = p.z - center.z;
                if (dx * dx + dy * dy + dz * dz <= squared) {
                    result.push(object);
                    if (result.length === maxResults) break;
                }
            }
            return result;
        }

        static findPawn() {
            const index = engine.player.controller.controlledIndex();
            return index < 0 ? null : Scene.get(index);
        }
        // Deprecated name retained as a thin alias; no duplicate O(n) scan.
        static findPlayer() { return Scene.findPawn(); }

        static moveStatic(index, position) {
            if (!position || ![position.x, position.y, position.z].every(Number.isFinite))
                throw new TypeError("Scene position requires finite x/y/z");
            if (Math.max(Math.abs(position.x), Math.abs(position.y),
                         Math.abs(position.z)) > 1e6)
                throw new RangeError("Scene position is outside world bounds");
            return engine.scene.transforms.moveStatic(index,
                position.x, position.y, position.z);
        }

        static rotateStatic(index, rotation) {
            if (!Number.isSafeInteger(index) || index < 0 ||
                !rotation || ![rotation.x, rotation.y, rotation.z].every(Number.isFinite))
                throw new TypeError("Scene rotation requires a valid entity index and finite Euler angles");
            if (Math.max(Math.abs(rotation.x), Math.abs(rotation.y),
                         Math.abs(rotation.z)) > 360000)
                throw new RangeError("Scene rotation exceeds the supported limits");
            return engine.scene.transforms.rotateStatic(index,
                rotation.x, rotation.y, rotation.z);
        }

        static setColor(index, rgb) {
            if (!Number.isInteger(rgb) || rgb < 0 || rgb > 0xffffff)
                throw new RangeError("Scene RGB color must be 0x000000..0xFFFFFF");
            return engine.scene.transforms.setColor(index, rgb);
        }

        /** Render presentation of an existing skinned Entity, not a prefab swap.
         *  The camera rig stays an independent scene object. */
        static setVisualMode(index, mode, settings = {}) {
            if (!Number.isSafeInteger(index) || index < 0 ||
                (mode !== "world" && mode !== "first_person_arms" &&
                 mode !== "first_person_body"))
                throw new TypeError("Invalid scene visual mode");
            if (settings == null || typeof settings !== "object")
                throw new TypeError("Invalid camera presentation settings");
            const forward = settings.forward ?? 0.55;
            const drop = settings.drop ?? 0.98;
            const pitch = settings.armPitch ?? 55;
            // Authored sight-line lateral offset (metres, +right). This is
            // also used by the attached character's two-hand grip solver.
            const right = settings.right ?? 0;
            // Explicitly tag aim-down-sights; no arbitrary drop-height heuristic.
            const ads = settings.ads === true;
            const recoilBack = settings.recoilBack ?? 0;
            const recoilArm = settings.recoilArm ?? 0;
            if (settings.ads !== undefined && typeof settings.ads !== "boolean")
                throw new TypeError("ADS presentation state must be boolean");
            if (![forward, drop, pitch, right,recoilBack,recoilArm].every(Number.isFinite) ||
                recoilBack < 0 || recoilBack > 0.06 ||
                recoilArm < 0 || recoilArm > 1 ||
                forward < 0 || forward > 3 || drop < 0 || drop > 3 ||
                Math.abs(pitch) > 180 || Math.abs(right) > 1)
                throw new RangeError("Invalid first-person presentation/grip offsets");
            return engine.scene.animation.setVisualMode(index, mode, forward, drop,
                pitch, right, ads, recoilBack, recoilArm);
        }

        // Per-entity native 8-weight YDD/YMT playback. The C++ host owns
        // VFS validation, clip selection and renderer synchronization.
        // Native scene-graph socket attachment; generic for any skinned YDD.
        // The attachment is evaluated with the same joint pose as rendering.
        static attachToBone(childIndex,parentIndex,bone,offset={},rotation={},alignment="socket") {
            if(!Number.isSafeInteger(childIndex) || childIndex<0 ||
               !Number.isSafeInteger(parentIndex) || parentIndex<0 ||
               typeof bone!=="string" || !/^[A-Za-z0-9_]{1,96}$/.test(bone))
                throw new TypeError("Invalid scene bone attachment");
            const pos=[offset.x??0,offset.y??0,offset.z??0];
            const angles=[rotation.x??0,rotation.y??0,rotation.z??0];
            if(![...pos,...angles].every(x=>typeof x==="number" &&
                Number.isFinite(x) && Math.abs(x)<=360))
                throw new RangeError("Invalid bone socket offset/angles");
            if(!["socket","camera_forward"].includes(alignment))
                throw new RangeError("Unknown bone alignment mode");
            return engine.scene.animation.attachBone(childIndex,parentIndex,bone,...pos,...angles,alignment);
        }

        // A weapon-local point transformed by the REAL skinned attachment.
        // A missing skeletal frame returns null rather than a camera guess.
        static attachmentPoint(childIndex, offset) {
            if(!Number.isSafeInteger(childIndex) || childIndex<0 ||
               !offset || ![offset.x,offset.y,offset.z].every(Number.isFinite))
                throw new TypeError("Scene.attachmentPoint needs entity index and local xyz");
            return engine.scene.animation.attachmentPoint(
                childIndex,offset.x,offset.y,offset.z);
        }

        static detachFromBone(childIndex) {
            if(!Number.isSafeInteger(childIndex)||childIndex<0)
                throw new RangeError("Invalid scene bone attachment index");
            return engine.scene.animation.detachBone(childIndex);
        }

        static playAnimation(index, clipReference, playbackRate = 1) {
            if (!Number.isFinite(playbackRate) || playbackRate < 0.1 || playbackRate > 4)
                throw new RangeError("Animation playback rate must be 0.1..4");
            if (!Number.isSafeInteger(index) || index < 0 ||
                typeof clipReference !== "string" ||
                !/^[^\\:@]+\.ycd@[^\\/:@]+$/.test(clipReference) ||
                clipReference.includes("..") || clipReference.length > 512)
                throw new TypeError("Expected native YCD dictionary@clip reference");
            return engine.scene.animation.setAnimation(index, clipReference, playbackRate);
        }

        static stopAnimation(index) {
            if (!Number.isSafeInteger(index) || index < 0)
                throw new RangeError("Scene animation index must be nonnegative");
            return engine.scene.animation.setAnimation(index, "");
        }
    }
