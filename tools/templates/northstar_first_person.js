// NorthStar First Person Platform — project-owned gameplay.
// Copyright (C) 2026 Kayla's Systems — Kayla Verner.
// The engine implements input, physics, camera and rendering, never these rules.
(() => {
    "use strict";
    const projectTitle = "__NORTHSTAR_PROJECT_TITLE__";
    const KEY = Object.freeze({
        W: 87, A: 65, S: 83, D: 68, JUMP: 32, USE: 69,
        ESCAPE: 27, EXIT: 121 // F10
    });
    const config = Object.freeze({
        mouseSensitivity: 0.0024, maxPitch: 1.4,
        interactRange: 3.6, interactRadius: 0.28,
        pushImpulse: 9, eyeHeight: 1.45
    });
    let yaw = 0, pitch = 0, paused = false;
    let previousJump = false, previousUse = false;
    let previousPause = false, previousExit = false;
    let playerIndex = -1;

    const pressed = key => engine.input.keyboard.isDown(key);
    const controller = () => {
        for (let i = 0; i < 4; ++i) {
            const pad = engine.input.gamepads.gamepad(i);
            if (pad) return pad;
        }
        return null;
    };
    function title() {
        engine.ui.viewport.setTitle(paused
            ? projectTitle + " | PAUSED"
            : projectTitle + " | WASD Move | Mouse Look | Space Jump | E Push");
    }
    function playerObject() {
        for (let start = 0; ; start += 256) {
            const page = engine.scene.entities.objects(start, 256);
            const player = page.find(object => object.player);
            if (player) return player.index;
            if (page.length < 256) return -1;
        }
    }
    function interact() {
        const eye = engine.player.motion.eye();
        const dx = Math.sin(yaw) * Math.cos(pitch);
        const dy = Math.sin(pitch);
        const dz = Math.cos(yaw) * Math.cos(pitch);
        const hit = engine.physics.queries.sweepSphere(
            eye.x, eye.y, eye.z,
            eye.x + dx * config.interactRange,
            eye.y + dy * config.interactRange,
            eye.z + dz * config.interactRange,
            config.interactRadius, playerIndex);
        if (!hit) return;
        const object = engine.scene.entities.objects(hit.index, 1)[0];
        if (object && object.mass > 0 && !object.player) {
            engine.physics.bodies.applyImpulse(hit.index,
                dx * config.pushImpulse,
                dy * config.pushImpulse + 1.2,
                dz * config.pushImpulse);
        }
    }
    engine.on("start", () => {
        playerIndex = playerObject();
        if (playerIndex < 0) throw Error("Scene has no player actor");
        engine.player.motion.setEyeHeight(config.eyeHeight);
        engine.logger.info("Project-controlled first-person gameplay initialized");
        title();
    });
    engine.on("update", deltaTime => {
        const pad = controller();
        const buttons = pad ? pad.buttons : 0;
        const pauseKey = pressed(KEY.ESCAPE) || Boolean(buttons & 0x0010);
        if (pauseKey && !previousPause) {
            paused = !paused;
            engine.session.lifecycle.pause(paused);
            title();
        }
        previousPause = pauseKey;
        const exitKey = pressed(KEY.EXIT) || Boolean(buttons & 0x0020);
        if (exitKey && !previousExit) engine.session.lifecycle.quit();
        previousExit = exitKey;
        if (paused) {
            engine.player.motion.move(0, 0);
            return;
        }
        const pointer = engine.input.pointer.pointerDelta();
        yaw += pointer.x * config.mouseSensitivity +
            (pad ? pad.rightX * 2.5 * deltaTime : 0);
        pitch = Math.max(-config.maxPitch,
            Math.min(config.maxPitch, pitch - pointer.y * config.mouseSensitivity +
                (pad ? pad.rightY * 2.1 * deltaTime : 0)));
        if (Math.abs(yaw) > Math.PI) yaw %= 2 * Math.PI;
        engine.camera.views.setAngles(yaw, pitch);

        let horizontal = Number(pressed(KEY.D)) - Number(pressed(KEY.A)) +
            (pad ? pad.leftX + Number(Boolean(buttons & 0x0008)) -
                Number(Boolean(buttons & 0x0004)) : 0);
        let forward = Number(pressed(KEY.W)) - Number(pressed(KEY.S)) +
            (pad ? pad.leftY + Number(Boolean(buttons & 0x0001)) -
                Number(Boolean(buttons & 0x0002)) : 0);
        const length = Math.hypot(horizontal, forward);
        if (length > 1) { horizontal /= length; forward /= length; }
        engine.player.motion.move(
            horizontal * Math.cos(yaw) + forward * Math.sin(yaw),
            forward * Math.cos(yaw) - horizontal * Math.sin(yaw));

        const jump = pressed(KEY.JUMP) || Boolean(buttons & 0x1000);
        if (jump && !previousJump) engine.player.motion.jump();
        previousJump = jump;

        const use = pressed(KEY.USE) || Boolean(buttons & 0x4000);
        if (use && !previousUse) interact();
        previousUse = use;
    });
    engine.on("stop", () => {
        engine.logger.info("Project-controlled first-person gameplay stopped");
    });
})();
