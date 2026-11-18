# Weapon muzzle, translucent VFX and physical casing contacts — 2026-11-17

## Source of the reported artifacts

The scripted firearm emitted its effect at the project-defined local muzzle point. That point had never been calibrated against the native YDD barrel geometry; the viewport drew all transient visuals as opaque RGB cube proxies. The imported YPT references exist, but their metadata explicitly warns that original REDengine GPU shaders and atlas textures have *not* been translated into executable NorthStar particle shaders. Presence of a YPT file is therefore not proof it renders its original effect.

## Muzzle calibration (from the actual pistol.ydd mesh)

The NEF8-compressed native YDD part 1/4 front face at z>=0.2355 m has symmetric X bounds about zero and vertex centre around x=0.00005, y=0.07381 m. The maximum local front-face Z is 0.235774 m. The previous project muzzle was (0,0.066,0.238), off the bore centre by roughly 7.8 mm in Y. The new demo and template profile uses (0,0.0738,0.238) — 2.2 mm in front of the actual front face — and still resolves it through the *animated weapon attachment transform*, not the camera eye.

## Native GPU presentation

`engine/northstarcpp/modules/viewport/viewport_weapon_fx_sprites.hpp`
adds a reusable provider-neutral translucent textured billboard path. The sprite RGBA textures are smooth procedurally authored flash/spark/smoke fallbacks with alpha feathering; the existing D3D12/D3D11 Materials V2 translucent path submits them in three shared GPU batches. These do not create physics bodies. Legacy external providers without Materials V2 retain the opaque proxy fallback.

This removes the pixelated opaque cubes on Materials V2 providers, but does *not* claim parity with the original imported YPT effects: the YPT GPU materials/texture atlases still require a dedicated translator/runtime particle pipeline.

## Persistent physical brass and NAC

Each shot creates the prefab `prefabs/weapon/pistol_shell.nprefab`, using `models/weapon/pistol/pistol_shell.ydd`, at the animated ejection socket. A small directional impulse is applied to a real runtime Jolt dynamic body. It is never destroyed by the short-lived muzzle effect.

A generic optional physics-provider extension was added:
- `NORTHSTARCPP_PHYSICS_BODY_MATERIAL_MODULE_ABI` (0x50484D01) with `configure(world,index,friction,restitution)`.
- Jolt implements dynamic per-body `SetFriction` and `SetRestitution`.
- `Physics.setBodyMaterial(index, {friction:.30,restitution:.42})` configures it without ABI-4 layout changes, with fallback warning for providers lacking the extension.
- `Physics.pollContacts(max)` drains provider Begin/End contact pairs; ground index is -1.

`WeaponEffects.update(dt)` observes *real* Begin/Block contacts for tracked casing indices. It selects the existing NAC groups `shell_contact_hard_01..04`, `shell_contact_medium_01..03`, `shell_contact_small_01..03`, `shell_contact_soft_01..05` and `shell_roll_01..03`. Events are de-duplicated within 75 ms and rolling is triggered only after multiple actual casing contacts. The first impact is stronger, later rebounds softer. Current cue intensity is based on bounce count, not yet on measured contact impulse; dynamic event speed/impulse is a future telemetry extension.

`Audio.playNacAt` now sets spatial voice coordinates to the current rigid-body position, with compatibility playback on ABI-1 providers. The Player updates the audio listener with the active camera pose.

## Validation

- `node tests/weapon_shell_contacts_smoke.cjs`: PASS, including contact-only sound, spatial position, impact progression, roll and physical material call.
- `node tests/weapon_effects_shared_smoke.cjs`: PASS.
- `node tests/weapon_ballistics_shared_smoke.cjs`: PASS.
- `node tests/abby_pistol_demo_smoke.cjs`: PASS.
- `cmake --build out/build --target northstarcpp_player northstarcpp_physics_jolt -j 4`: PASS.
- Jolt scene runtime and particle asset catalog native CTests: PASS.
- Actual AbbyPistolDemo Player startup: alive after 11 s, no project-script errors, native weapon bone attachment PASS.
- Two broader existing Jolt CTests, `northstarcpp_jolt_selected` and `northstarcpp_jolt_physics`, currently FAIL at `Player did not stop against a static body`; their failure remains separate and cannot be considered closed.

## Still to verify visually

- Capture actual D3D12 GPU frames after firing and compare muzzle sprite location against the animated barrel tip under ADS/hip fire/recoil.
- End-to-end audible NAC shell contacts and measured restitution on Jolt floor/metal/wood, including FPS/time dilation.
- Original imported YPT GPU shaders and their texture atlases are not translated; current sprite path is an antialiased procedural material fallback.
- The shell model currently has one unresolved YMAT slot and falls back to neutral PBR.
- No production dist was overwritten; no commit or push.
