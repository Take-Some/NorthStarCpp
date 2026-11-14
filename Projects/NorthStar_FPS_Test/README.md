# NorthStar FPS Test

Native first-person shooter test project for NorthStarCpp 0.4.0.

## Test scenario

- One player-controlled first-person actor with gravity and collision.
- 677 authored objects forming a dense test arena.
- Nine shootable targets, including four animated moving targets.
- Three hazards that test health/damage handling.
- Configurable weapons, target colors and camera settings.
- Sound effects for shooting and reloading.
- Current native FPS Arena gameplay; optional V8 is not required.

## Controls

| Input | Action |
| --- | --- |
| WASD / arrow keys | Walk |
| Mouse | First-person aim |
| Left mouse | Fire |
| R | Reload |
| Space | Jump |
| Escape | Pause / menu |

## Start

Double-click run.cmd. It selects the verified QA engine package at:

    out/qa-fps-dist-20261114/NorthStarCpp-0.4.0-windows-x64-Release-QA/bin/

The runner never silently falls back to an unverified production dist.

Native direct command:

    northstarcpp_player.exe --fps --scene Projects/NorthStar_FPS_Test/main.nscn

Headless verification: verify.cmd

## Project structure

- northstarcpp.project.json — game_mode=fps metadata.
- main.nscn — NEF8 Scene ListFile v2 (content kind 6).
- assets/gameplay/fps.json — magazine, reload, target and hazard tuning.
- assets/gameplay/camera.json — first-person camera configuration.
- assets/audio/shot.wav and reload.wav — weapon audio.
- assets/models, materials, textures — local sky assets.

Native FPS gameplay deliberately does not load a project scripts/main.js
entrypoint. Script integration can be verified separately with V8.

## QA restrictions

The QA engine is built from current sources and verified with CTests and
installed-player checks. It is not a public release; production promotion
requires independently signed clean Windows acceptance.
