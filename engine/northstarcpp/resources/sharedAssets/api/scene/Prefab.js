// NorthStarCpp prefab instances are groups of real script-owned ECS entities.
// Templates are read exclusively through the replaceable engine.prefab VFS API.
(() => {
    "use strict";

    class PrefabInstance {
        #entities;
        #root;

        constructor(entities, root) {
            this.#entities = Object.freeze([...entities]);
            this.#root = root;
        }

        get root() { return this.#root; }
        get entities() { return this.#entities; }
        get alive() { return this.#root.alive; }

        destroy() {
            let count = 0;
            // Child entities precede parents in reverse topological order.
            for (let i = this.#entities.length - 1; i >= 0; --i) {
                if (this.#entities[i].destroy()) ++count;
            }
            return count;
        }
    }

    class Prefab {
        static instantiate(path, options = {}) {
            if (typeof path !== "string" ||
                !/^prefabs\/[A-Za-z0-9_./-]+\.nprefab$/.test(path) ||
                path.includes("..") || path.includes("//"))
                throw new TypeError("Prefab path must be a VFS path under prefabs/");
            if (!options || typeof options !== "object" || Array.isArray(options))
                throw new TypeError("Prefab options must be an object");
            const {
                position: anchor = {x: 0, y: 0, z: 0},
                collision = undefined,
                physics = "authored"
            } = options;
            if (Object.keys(options).some(name =>
                    !["position", "collision", "physics"].includes(name)) ||
                ![anchor?.x, anchor?.y, anchor?.z].every(Number.isFinite) ||
                (collision !== undefined && typeof collision !== "boolean") ||
                !["authored", "visual"].includes(physics) ||
                (physics === "visual" && collision === true))
                throw new TypeError("Prefab instance requires valid anchor and physics options");

            const descriptor = engine.scene.prefabs.read(path);
            if (!descriptor || !Array.isArray(descriptor.objects) ||
                descriptor.objects.length === 0 || descriptor.objects.length > 128)
                throw new Error("Invalid prefab template from VFS");

            // Validate and order the entire graph before mutating runtime state.
            // Linear dependency walk: validate the complete prefab before
            // creating a single native entity, preserving parent-before-child.
            const definitions = new Map();
            for (const object of descriptor.objects) {
                if (!object || typeof object.id !== "string" ||
                    !/^[1-9][0-9]*$/.test(object.id) ||
                    definitions.has(object.id))
                    throw new Error("Invalid prefab entity stable ID");
                definitions.set(object.id, object);
            }
            const ordered = [];
            const visiting = new Set();
            const settled = new Set();
            const visit = id => {
                if (settled.has(id)) return;
                const entity = definitions.get(id);
                if (!entity || visiting.has(id) ||
                    typeof entity.parent !== "string")
                    throw new Error("Prefab contains a missing parent or hierarchy cycle");
                visiting.add(id);
                if (entity.parent !== "0") visit(entity.parent);
                if (!entity.position ||
                    ![entity.position.x, entity.position.y,
                      entity.position.z].every(Number.isFinite))
                    throw new Error("Prefab contains invalid local transform");
                visiting.delete(id);
                settled.add(id);
                ordered.push(entity);
            };
            for (const id of definitions.keys()) visit(id);
            const roots = ordered.filter(entity => entity.parent === "0");
            if (roots.length !== 1)
                throw new Error("Prefab must contain exactly one root");

            // Generic animation components belong to the model prefab,
            // never to Player or a weapon system. Resolve every reference
            // before spawning anything so failed templates are atomic.
            const animations = descriptor.animations ?? [];
            if (!Array.isArray(animations) || animations.length > ordered.length)
                throw new Error("Invalid prefab animation component list");
            const animated = new Set();
            for (const entry of animations) {
                if (!entry || typeof entry.entity !== "string" ||
                    !definitions.has(entry.entity) ||
                    animated.has(entry.entity) ||
                    typeof entry.clip !== "string" ||
                    !/^[A-Za-z0-9_./-]+\.ycd@[A-Za-z0-9_./-]+$/.test(entry.clip) ||
                    entry.clip.includes("..") || entry.clip.length > 512 ||
                    !/\.yd[dr]$/.test(definitions.get(entry.entity).asset))
                    throw new Error("Invalid prefab YCD animation component");
                animated.add(entry.entity);
            }

            const handles = new Map();
            const created = [];
            try {
                for (const definition of ordered) {
                    const position = {
                        x: anchor.x + definition.position.x,
                        y: anchor.y + definition.position.y,
                        z: anchor.z + definition.position.z
                    };
                    const parent = definition.parent === "0" ?
                        null : handles.get(definition.parent);
                    const entity = NorthStar.modules.engine.Scene.spawn({
                        name: definition.name,
                        position,
                        size: definition.size,
                        // Visual native mesh units independent of collider.
                        scale: definition.scale ?? {x:1,y:1,z:1},
                        // Socket-bound presentation is not a simulated rigid
                        // body. The prefab definition retains its physical mass.
                        mass: physics === "visual" ? 0 : definition.mass,
                        color: definition.color,
                        asset: definition.asset,
                        layer: definition.layer,
                        collision: physics === "visual" ? false :
                            (collision === undefined ? definition.collision !== false : collision),
                        parent
                    });
                    handles.set(definition.id, entity);
                    created.push(entity);
                }
                for (const animation of animations) {
                    const target = handles.get(animation.entity);
                    NorthStar.modules.engine.Scene.playAnimation(
                        target.index, animation.clip);
                }
            } catch (error) {
                // A failed prefab import must leave no orphan physics bodies,
                // even when a later child cannot be created.
                for (let i = created.length - 1; i >= 0; --i) {
                    try { created[i].destroy(); }
                    catch (_cleanupError) {
                        // Do not hide the original provider/asset failure.
                    }
                }
                throw error;
            }
            return new PrefabInstance(created, handles.get(roots[0].id));
        }
    }

    NorthStar.sceneTypes.registerPrefab(Prefab);
})();
