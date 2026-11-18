    class CameraViewController {
        #toggleKey;
        #transitionSeconds;
        #initialMode;
        #minDistance;
        #maxDistance;
        #wheelStep;
        #previousKeyDown = false;
        #followDistance = null;
        #started = false;

        constructor(options = {}) {
            this.#toggleKey = options.toggleKey === undefined ? 86 : options.toggleKey;
            this.#transitionSeconds = options.transitionSeconds === undefined
                ? 0.45 : options.transitionSeconds;
            this.#initialMode = options.initialMode || "first_person";
            this.#minDistance = options.minDistance === undefined ? 1.5 : options.minDistance;
            this.#maxDistance = options.maxDistance === undefined ? 12 : options.maxDistance;
            this.#wheelStep = options.wheelStep === undefined ? 0.7 : options.wheelStep;

            if (!Number.isInteger(this.#toggleKey) || this.#toggleKey < 0 ||
                this.#toggleKey > 255)
                throw new RangeError("Camera toggle key must be a virtual-key code (0..255)");
            this.#validateDuration(this.#transitionSeconds);
            if (!CameraViewController.MODES.includes(this.#initialMode))
                throw new RangeError("Invalid initial camera view");
            if (!Number.isFinite(this.#minDistance) || this.#minDistance < 0.5 ||
                !Number.isFinite(this.#maxDistance) ||
                this.#maxDistance <= this.#minDistance || this.#maxDistance > 100)
                throw new RangeError("Camera follow distance must be within 0.5..100 metres");
            if (!Number.isFinite(this.#wheelStep) || this.#wheelStep <= 0 ||
                this.#wheelStep > 10)
                throw new RangeError("Camera wheel step must be within (0, 10] metres");

            Object.freeze(this);
        }

        static get MODES() { return ["first_person", "third_person"]; }

        #validateDuration(value) {
            if (!Number.isFinite(value) || value < 0 || value > 10)
                throw new RangeError("Camera transition duration must be 0..10 seconds");
        }

        #clamp(value) {
            return Math.max(this.#minDistance, Math.min(this.#maxDistance, value));
        }

        #initializeDistance() {
            const current = Camera.followDistance();
            if (!Number.isFinite(current))
                throw new RangeError("Camera follow distance is not finite");
            this.#followDistance = this.#clamp(current);
            if (this.#followDistance !== current)
                Camera.setFollowDistance(this.#followDistance);
        }

        start() {
            this.#initializeDistance();
            // A key held during startup must not immediately reverse the view.
            this.#previousKeyDown = Input.isDown(this.#toggleKey);
            Camera.setMode(this.#initialMode, 0);
            this.#started = true;
        }

        stop() {
            this.#started = false;
            this.#previousKeyDown = false;
        }

        mode() { return Camera.mode(); }

        distance() {
            if (this.#followDistance === null) this.#initializeDistance();
            return this.#followDistance;
        }

        setMode(mode, duration = this.#transitionSeconds) {
            if (!CameraViewController.MODES.includes(mode))
                throw new RangeError("Unknown camera view");
            this.#validateDuration(duration);
            Camera.setMode(mode, duration);
            return mode;
        }

        toggle() {
            return this.setMode(this.mode() === "first_person"
                ? "third_person" : "first_person");
        }

        update() {
            if (!this.#started) return;
            const down = Input.isDown(this.#toggleKey);
            if (down && !this.#previousKeyDown) this.toggle();
            this.#previousKeyDown = down;

            // Wheel units are signed, fractional notches. Positive is closer.
            const steps = Input.wheelDelta();
            if (!Number.isFinite(steps))
                throw new RangeError("Non-finite camera wheel input");
            if (this.mode() !== "third_person" || steps === 0) return;
            if (this.#followDistance === null) this.#initializeDistance();
            const next = this.#clamp(this.#followDistance - steps * this.#wheelStep);
            if (next !== this.#followDistance) {
                Camera.setFollowDistance(next);
                this.#followDistance = next;
            }
        }
    }

    class CameraService {
        #defaultScript = null;

        constructor() {
            Object.defineProperty(this, "ViewController", {
                value: CameraViewController, enumerable: true
            });
            Object.freeze(this);
        }

        createViewController(options = {}) {
            return new CameraViewController(options);
        }

        installDefaultScript(ScriptType) {
            if (this.#defaultScript !== null)
                throw new Error("Default player camera script is already installed");
            if (typeof ScriptType !== "function")
                throw new TypeError("Default camera script must be a class");
            const instance = new ScriptType();
            instance.install();
            this.#defaultScript = instance;
            return instance;
        }

        get defaultScript() { return this.#defaultScript; }
    }

    // A module registers exactly one class. This service is allocated once
    // per V8 context; instances retain their own private runtime state.
