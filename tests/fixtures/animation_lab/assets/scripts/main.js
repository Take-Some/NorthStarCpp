// Portable scene animation fixture; no standalone sample project required.
engine.on("start", () => engine.animation.clips.play("3", "animations/lift.anim.json", true, 1));
engine.on("stop", () => engine.animation.clips.stop("3"));
