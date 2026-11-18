    // Shared vehicle controls; project rig configuration remains in .nvehicle.
    class VehicleHandle {
        #index;
        constructor(index) {
            this.#index = Contracts.index(index, "Vehicle index");
        }
        get index() { return this.#index; }
        get state() { return Vehicle.state(this.#index); }
        get motion() { return Vehicle.motion(this.#index); }
        get powertrain() { return Vehicle.powertrainState(this.#index); }
        drive(controls) { return Vehicle.drive(this.#index, controls); }
        navigate(destination) { return Vehicle.navigate(this.#index, destination); }
        selectGear(gear) { return Vehicle.selectGear(this.#index, gear); }
        stop() { return Vehicle.drive(this.#index, {brake: 1}); }
        detach() { return Vehicle.detach(this.#index); }
    }
    class Vehicle {
        static #index(value) { return Contracts.index(value, "Vehicle index"); }
        static #number(value, name, min, max) {
            return Contracts.number(value,name,min,max);
        }
        static available() { return typeof engine.vehicle.drive?.controls === "function"; }
        /** Non-owning handle; never spawns a duplicate vehicle. */
        static handle(index) { return new VehicleHandle(Vehicle.#index(index)); }
        static attach(index, wheelbase, maxSpeed) {
            Vehicle.#index(index);
            Vehicle.#number(wheelbase, "wheelbase", 0.1, 100);
            Vehicle.#number(maxSpeed, "maxSpeed", 0.1, 1000);
            return engine.vehicle.core.attach(index, wheelbase, maxSpeed);
        }
        static configureRig(index, path) {
            Vehicle.#index(index);
            return engine.vehicle.drive.configureRig(index,
                Contracts.vfs(path, ".nvehicle"));
        }
        static controls(index, throttle, steer, brake) {
            Vehicle.#index(index);
            Vehicle.#number(throttle, "throttle", -1, 1);
            Vehicle.#number(steer, "steer", -1, 1);
            Vehicle.#number(brake, "brake", 0, 1);
            return engine.vehicle.drive.controls(index, throttle, steer, brake);
        }
        static drive(index, options = {}) {
            const {throttle = 0, steer = 0, brake = 0} =
                Contracts.options(options,"Vehicle controls");
            return Vehicle.controls(index, throttle, steer, brake);
        }
        static waypoint(index, x, z, speed, radius = 2) {
            Vehicle.#index(index);
            Vehicle.#number(x, "waypoint.x", -1e6, 1e6);
            Vehicle.#number(z, "waypoint.z", -1e6, 1e6);
            Vehicle.#number(speed, "waypoint.speed", 0, 1000);
            Vehicle.#number(radius, "waypoint.radius", 0.1, 1000);
            return engine.vehicle.navigation.waypoint(index, x, z, speed, radius);
        }
        static navigate(index, options = {}) {
            const {x, z, speed, radius = 2} =
                Contracts.options(options,"Vehicle destination");
            return Vehicle.waypoint(index, x, z, speed, radius);
        }
        static state(index) { return engine.vehicle.core.state(Vehicle.#index(index)); }
        static motion(index) { return engine.vehicle.core.motion(Vehicle.#index(index)); }
        static powertrainState(index) { return engine.vehicle.drive.powertrainState(Vehicle.#index(index)); }
        static selectGear(index, gear) {
            Vehicle.#index(index);
            if (!Number.isSafeInteger(gear) || gear < -1 || gear > 8)
                throw new RangeError("Gear must be -1, 0 or 1..8");
            return engine.vehicle.drive.selectGear(index, gear);
        }
        static wheelState(index, wheel) { return engine.vehicle.wheels.wheelState(Vehicle.#index(index), wheel); }
        static wheelPose(index, wheel) { return engine.vehicle.wheels.wheelPose(Vehicle.#index(index), wheel); }
        static wheelCount(index) { return engine.vehicle.wheels.wheelCount(Vehicle.#index(index)); }
        static seatCount(index) { return engine.vehicle.seats.seatCount(Vehicle.#index(index)); }
        static seatState(index, seat) { return engine.vehicle.seats.seatState(Vehicle.#index(index), seat); }
        static occupy(index, seat, entity) { return engine.vehicle.seats.occupy(Vehicle.#index(index), seat, entity); }
        static release(index, seat, entity) { return engine.vehicle.seats.release(Vehicle.#index(index), seat, entity); }
        static setDoor(index, door, open) { return engine.vehicle.doors.setDoor(Vehicle.#index(index), door, open); }
        static doorCount(index) { return engine.vehicle.doors.doorCount(Vehicle.#index(index)); }
        static doorState(index, door) { return engine.vehicle.doors.doorState(Vehicle.#index(index), door); }
        static detach(index) { return engine.vehicle.core.detach(Vehicle.#index(index)); }
        static provider() { return engine.vehicle.provider.provider(); }
        static count() { return engine.vehicle.core.count(); }
    }
