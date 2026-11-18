    // Shared geometric collision queries across all games and native providers.
    class Collision {
        static raycast(from,to,ignoreIndex=-1) {
            return Physics.raycast(from,to,ignoreIndex);
        }
        static sweepSphere(from, to, radius = 0.025, ignoreIndex = -1) {
            return Physics.sweepSphere(from, to, radius, ignoreIndex);
        }
        static lineOfSight(from, to, {radius = 0.01, ignoreIndex = -1} = {}) {
            return Collision.sweepSphere(from, to, radius, ignoreIndex) === null;
        }
        /** Position of the swept center at first contact (not the surface point). */
        static traceSphere(from, to, radius = 0.025, ignoreIndex = -1) {
            const hit = Collision.sweepSphere(from, to, radius, ignoreIndex);
            if (!hit) return null;
            const f = hit.fraction;
            if (typeof f !== "number" || !Number.isFinite(f) || f < 0 || f > 1)
                throw new Error("Physics provider returned invalid hit fraction");
            const point = Object.freeze({
                x: from.x + (to.x - from.x) * f,
                y: from.y + (to.y - from.y) * f,
                z: from.z + (to.z - from.z) * f
            });
            return Object.freeze({hit, point});
        }
    }
    // Engine-side effect providers are optional and replaceable. Weapon muzzle
    // FX are supported by the current native viewport, generic particles aren't.
