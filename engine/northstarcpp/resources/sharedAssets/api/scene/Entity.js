// Runtime entity handle. The native scene and physics providers own the object.
(() => {
    "use strict";

    class Entity {
        #id;
        #index;
        #destroyed = false;

        constructor(record) {
            if (!record || typeof record.id !== "string" ||
                !Number.isSafeInteger(record.index) || record.index < 0)
                throw new TypeError("Invalid runtime entity handle");
            this.#id = record.id;
            this.#index = record.index;
        }

        get id() { return this.#id; }
        get index() { return this.#index; }
        get alive() {
            return !this.#destroyed &&
                engine.scene.entities.objects(this.#index, 1)[0]?.id === this.#id;
        }

        get snapshot() {
            if (!this.alive) return null;
            const record = engine.scene.entities.objects(this.#index, 1)[0];
            return record?.id === this.#id ? record : null;
        }

        get position() { return this.snapshot?.position ?? null; }
        getComponent(name) {
            if(!this.alive)throw new Error("Runtime entity was destroyed");
            return NorthStar.modules.engine.ECS.get(this,name);
        }
        hasComponent(name) {
            if(!this.alive)return false;
            return NorthStar.modules.engine.ECS.has(this,name);
        }
        setComponent(name,value) {
            if(!this.alive)throw new Error("Runtime entity was destroyed");
            NorthStar.modules.engine.ECS.set(this,name,value);
            return this;
        }


        setPosition(position) {
            if (!this.alive) throw new Error("Runtime entity was destroyed");
            if (!position || ![position.x, position.y, position.z].every(Number.isFinite))
                throw new TypeError("Position requires finite x/y/z");
            engine.scene.transforms.moveRuntime(this.#id, position.x, position.y, position.z);
            return this;
        }

        setRotation(eulerDegrees) {
            if(!this.alive)throw new Error("Runtime entity was destroyed");
            if(!eulerDegrees ||
               ![eulerDegrees.x,eulerDegrees.y,eulerDegrees.z].every(Number.isFinite))
                throw new TypeError("Rotation requires finite Euler XYZ degrees");
            engine.scene.transforms.rotateRuntime(this.#id,eulerDegrees.x,
                eulerDegrees.y,eulerDegrees.z);
            return this;
        }

        setColor(rgb) {
            if (!this.alive) throw new Error("Runtime entity was destroyed");
            if (!Number.isInteger(rgb) || rgb < 0 || rgb > 0xFFFFFF)
                throw new RangeError("RGB color is outside 0x000000..0xFFFFFF");
            engine.scene.transforms.setColor(this.#index, rgb);
            return this;
        }

        applyImpulse(vector) {
            if (!this.alive) throw new Error("Runtime entity was destroyed");
            if (!vector || ![vector.x, vector.y, vector.z].every(Number.isFinite))
                throw new TypeError("Impulse requires finite x/y/z");
            engine.physics.bodies.applyImpulse(this.#index, vector.x, vector.y, vector.z);
            return this;
        }

        destroy() {
            if (this.#destroyed) return false;
            if (!this.alive) {
                this.#destroyed = true;
                return false;
            }
            engine.scene.entities.destroy(this.#id);
            this.#destroyed = true;
            return true;
        }
    }

    NorthStar.sceneTypes.registerEntity(Entity);
})();
