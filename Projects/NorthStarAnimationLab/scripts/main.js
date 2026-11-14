// NorthStarAnimationLab: project-owned animation lifecycle.
(() => {
    "use strict";
    const animatedEntityId = "3";
    const animationAsset = "animations/lift.anim.json";
    engine.on("start", () => {
        engine.animation.play(animatedEntityId, animationAsset, true, 1);
    });
    engine.on("stop", () => {
        engine.animation.stop(animatedEntityId);
    });
})();
