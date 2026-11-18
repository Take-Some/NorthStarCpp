    // One engine-owned validation contract. All high-level service facades
    // delegate to host providers; no secondary physics/scene implementation.
    class Contracts {
        static options(value, label = "Options") {
            if (value === null || typeof value !== "object" ||
                Array.isArray(value))
                throw new TypeError(label + " must be an object");
            return value;
        }
        static number(value, label, min = -Infinity, max = Infinity) {
            if (typeof value !== "number" || !Number.isFinite(value) ||
                value < min || value > max)
                throw new RangeError(label + " must be a finite number within " +
                    min + ".." + max);
            return value;
        }
        static identifier(value, label = "Identifier") {
            if (typeof value !== "string" ||
                !/^[a-zA-Z0-9_.:-]{1,80}$/.test(value))
                throw new TypeError(label + " must be a valid identifier");
            return value;
        }
        static index(value, label = "Scene index") {
            if (!Number.isSafeInteger(value) || value < 0)
                throw new RangeError(label + " must be a nonnegative integer");
            return value;
        }
        static vector(value, label = "Vector3", maxAbs = 1e6) {
            if (!value || typeof value !== "object" || Array.isArray(value))
                throw new TypeError(label + " must be {x,y,z}");
            for (const axis of ["x","y","z"])
                Contracts.number(value[axis], label + "." + axis, -maxAbs, maxAbs);
            return value;
        }
        static vfs(path, extension, prefix = "") {
            if (typeof path !== "string" || path.length > 512 ||
                !/^[a-zA-Z0-9_./-]+$/.test(path) ||
                path.startsWith("/") || path.includes("..") || path.includes("//") ||
                (prefix && !path.startsWith(prefix)) ||
                (extension && !path.endsWith(extension)))
                throw new TypeError("Invalid project-relative VFS asset path: " + path);
            return path;
        }
    }
