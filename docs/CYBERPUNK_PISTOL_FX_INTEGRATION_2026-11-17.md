# WolvenKit / Cyberpunk 2077 — Pistol VFX analysis
Date: 2026-11-17

## Installed utilities
- Official WolvenKit.Console 9.1.1: out/tools/WolvenKit.Console-9.1.1
- Working compatibility WolvenKit.Console 8.20.0: out/tools/WolvenKit.Console-8.20.0
- Local .NET runtime 10.0.15: out/tools/dotnet
- Official 9.1.1 ZIP SHA256: A14EAE46931251635256E7B209CD4AA998AE5039A5FBD506FEFAAB671BF7F451
- Windows LongPathsEnabled=0, non-elevated session. Console 9.1.1 refuses to run; Console 8.20.0 warns but successfully runs unbundle and convert serialize. Neither the registry nor game archives were modified.

## Actual extraction
Source: C:\Games\Cyberpunk 2077\archive\pc\content (33 archives).
Extraction automation: tools/extract_cyberpunk_weapon_fx.ps1
Selected archives: basegame_1_engine, basegame_4_gamedata, basegame_4_appearance, basegame_3_nightcity.
Result: 2,748 .effect and .particle files were unbundled matching weapon/VFX keywords.
Output: out/fx_extract; manifest: out/fx_extract/weapon-fx-manifest.csv.

## Confirmed Militech Lexington references
- basegame_3_nightcity/base/fx/weapons/firearms/pistols/militech_lexington/w_pistol_lexington_muzzle.effect
- basegame_3_nightcity/base/fx/weapons/firearms/pistols/militech_lexington/w_pistol_lexington_ejection_port.effect
- basegame_4_gamedata/base/fx/weapons/firearms/pistols/militech_lexington/w_pistol_lexington_muzzle.particle
- basegame_4_gamedata/base/fx/weapons/firearms/pistols/militech_lexington/w_pistol_lexington_eject.particle
- basegame_4_gamedata/base/fx/weapons/bullet_shells/cal_9mm.particle

WolvenKit convert serialize decoded four files:
- out/fx_json/w_pistol_lexington_muzzle.effect.json (297,520 B)
- out/fx_json/w_pistol_lexington_muzzle.particle.json (129,064 B)
- out/fx_json/w_pistol_lexington_eject.particle.json (111,164 B)
- out/fx_json/cal_9mm.particle.json (23,282 B)

The real muzzle worldEffect has three particle events pointing at the muzzle system, eject system, and cal_9mm shell system. Serialized timeDuration values are 5, 5, 10; their physical units have not been established.

Muzzle CParticleSystem has five named emitter groups:
| Name | maxParticles |
| --- | ---: |
| muzzle_forward | 1 |
| muzzle_round | 4 |
| muzzle_round_smoke | 3 |
| sparks | 7 |
| muzzle_round_color | 1 |

Eject particle emitters: smoke (1), smoke_back (1), sparks (2), smoke_back_loc (1).
The cal_9mm particle has one shell emitter (max 1) with 3D rotation-rate initialization, velocity, acceleration, size-over-life and collision modules.

## Application in NorthStarCpp
- The self-authored FpsWeaponEffects module has been aligned to the five separate emitter families above, plus eject-port smoke.
- All transient effects remain original procedural render proxies, not REDengine binary assets. True transparent GPU particle rendering and exact replication of the game's shaders have not been implemented.
- The real cartridge behavior is implemented independently as persistent Jolt runtime bodies: one casing per fired round, no TTL; shells remain after flash/smoke expiry until the physics world resets. Cap: 2,048 objects per session to prevent unbounded memory use; additional spawns fail explicitly.
- FPS targeting ignores the Weapon debris layer, and the projectile ray is captured before camera recoil to maintain aiming accuracy.

## QA / release limits
- Tests northstarcpp_fps_weapon_effects and northstarcpp_fps_arena passed before the five-emitter refinement.
- Latest modified fps_weapon_effects.cpp object compiles successfully; full build currently blocked by a concurrent unrelated scene_world.cpp type error (extent(ObjectBounds) passed to extent(SceneObject)).
- End-to-end Player pistol test previously timed out at 45 seconds. No GPU visual acceptance has been completed.
- NorthStar does not load .effect/.particle directly. Extracted copyrighted game assets remain local research data under out/, not redistributable content in source assets or release.
- No game install files changed; no commit/push or production dist replacement.
