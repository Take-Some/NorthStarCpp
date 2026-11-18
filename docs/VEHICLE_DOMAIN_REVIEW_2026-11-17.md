> Paths reflect the grouped OOP engine SDK layout. The canonical load order is api/manifest.json.

# NorthStarCpp — Vehicle Domain Review

17 November 2026. Vehicles.zip: 112 entries (~7.2 MB extracted). Source files from the Rockstar/RAGE vehicle subsystem; no third-party source was copied into NorthStarCpp. Review only.

## Scope

VehicleFactory, automobile handling, wheel, transmission, damage, seats, doors, lights, population and roads; specialized bikes, boats, aircraft, submarine, trains and trailers. Dependencies are not portable to NorthStarCpp. Archive is source code rather than models.

## Delivered

- Optional provider-neutral Powertrain ABI v4 (automatic/manual gears, up to eight forward ratios, reverse/neutral, RPM and shift limits, torque and telemetry); ABIs v1/v2/v3 preserved.
- .nvehicle v3 `<Powertrain>` authoring; v2 remains readable. MT28 demonstration: `Projects/KaylaNightCity/vehicles/mahir_mt28_powertrain_example.nvehicle`.
- Cached rig geometry and drive/brake coefficients, reusable transactional scratch buffer, efficient sorted-ID enumeration.
- VehicleService and script facade `@northstar/engine.Vehicle`; SDK source of truth: `api/core/sdk.js`, not generated bootstrap.js.

## QA

Native, Dynamics, Cabin, Powertrain, XML, World Scaling: 6/6 CTest passed. 128 vehicles × 180 frames: 28.73 ms total isolated CPU measurement (not before/after). Player and vehicle_native.dll built in out/build. JS Vehicle smoke and modular SDK composer verified.

## Outstanding

Powertrain v4 is currently for `contact_mode="flat"`; independent Jolt VehicleConstraint driveline integration is not done. The example does not replace the original bus configuration. Traffic road graph, obstacle avoidance, damage, specialized aircraft/marine/rail/2-wheeler providers, full scene/GPU QA and representative profiling remain open.

Next: (P0) Jolt torque integration, lane graph, traffic law, avoidance and safe braking; (P1) factory/editor, damage, lights, traffic pooling; (P2) specialized physics providers.

No commit/push performed.
