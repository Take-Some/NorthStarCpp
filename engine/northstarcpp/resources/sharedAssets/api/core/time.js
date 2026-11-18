    // Simulation-clock observations. Does not pretend to control native
    // physics/render time scale; all clock values come from engine update dt.
    class Time {
        static #delta = 0;
        static #elapsed = 0;
        static #frame = 0;
        static #disposeUpdate = null;
        static #disposeStop = null;
        static #clients = 0;

        static attach() {
            Time.#clients++;
            if (Time.#disposeUpdate) return;
            if (typeof engine.on !== "function")
                throw new Error("Game update event provider unavailable");
            Time.#disposeUpdate = Events.on("update", dt => {
                if (typeof dt !== "number" || !Number.isFinite(dt) || dt < 0)
                    return;
                Time.#delta = dt;
                Time.#elapsed += dt;
                Time.#frame++;
            });
            Time.#disposeStop = Events.on("stop", () => Time.detach());
        }
        static release() {
            if (Time.#clients>0)Time.#clients--;
            if (Time.#clients===0)Time.detach();
        }
        static detach() {
            Time.#clients=0;
            const update=Time.#disposeUpdate, stop=Time.#disposeStop;
            Time.#disposeUpdate=Time.#disposeStop=null;
            update?.();
            stop?.();
            Time.#delta=0;
        }
        static get delta() { return Time.#delta; }
        static get elapsed() { return Time.#elapsed; }
        static get frame() { return Time.#frame; }
    }

    class ScheduledTask {
        #cancel;
        constructor(cancel) {
            this.#cancel = cancel;
            Object.freeze(this);
        }
        cancel() { return this.#cancel(); }
    }

    // Script-owned deterministic timers. No JS setTimeout, OS wall clock,
    // or unrelated global update subscriptions per scheduled callback.
    class Scheduler {
        #tasks = new Set();
        #disposed = false;
        #error;
        constructor(onError = error => Logger.warn(
            "[engine.scheduler] " + String(error))) {
            if (typeof onError !== "function")
                throw new TypeError("Scheduler error handler must be callable");
            this.#error=onError;
        }
        #add(seconds, callback, repeating) {
            if (this.#disposed) throw new Error("Scheduler was disposed");
            Contracts.number(seconds,"Timer duration",repeating?1e-6:0,86400);
            if (typeof callback !== "function")
                throw new TypeError("Scheduled callback must be a function");
            if (this.#tasks.size >= 1024)
                throw new RangeError("Scheduler task limit exceeded");
            const record={remaining:seconds,period:seconds,callback,repeating};
            this.#tasks.add(record);
            let active=true;
            return new ScheduledTask(()=>{
                if (!active) return false;
                active=false;
                return this.#tasks.delete(record);
            });
        }
        after(seconds, callback) { return this.#add(seconds,callback,false); }
        every(seconds, callback) { return this.#add(seconds,callback,true); }
        update(dt) {
            if (this.#disposed) return;
            Contracts.number(dt,"Scheduler dt",0,1);
            // Bound catch-up work to avoid a stall after a hitch. Each task
            // can fire at most 8 times in one engine frame.
            for (const task of [...this.#tasks]) {
                if (!this.#tasks.has(task)) continue;
                task.remaining -= dt;
                for (let fired=0; task.remaining<=0 && fired<8; ++fired) {
                    if (!task.repeating) this.#tasks.delete(task);
                    else task.remaining += task.period;
                    try { task.callback(); }
                    catch (error) { this.#error(error); this.#tasks.delete(task); }
                    if (!this.#tasks.has(task)) break;
                }
                if (task.repeating && task.remaining<=0)
                    task.remaining=task.period;
            }
        }
        get pending() { return this.#tasks.size; }
        dispose() {
            if (this.#disposed) return false;
            this.#disposed=true;
            this.#tasks.clear();
            return true;
        }
    }
