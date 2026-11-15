// Optional project-owned gameplay. Root controllers in sharedAssets already
// handle player loading, locomotion, animation, interaction and camera views.
//
// Add game rules here: quests, triggers, objectives and scene events.
// See sharedAssets/scripts/README.md for the SDK and override examples.
import { Camera, Events, Logger } from "@northstar/engine";

Events.on("start", () => {
    Logger.info(`Project started (camera: ${Camera.mode()})`);
});

Events.on("stop", () => {
    Logger.info("Project stopped");
});
