import fs from "node:fs";
import path from "node:path";
import assert from "node:assert/strict";
import {createRequire} from "node:module";
const require=createRequire(import.meta.url);
const nui=require("./nui_test_bridge.cjs");

const root=process.cwd();
const files=["AbbyPistolDemo","NorthStarFirstPersonPlatform","KaylaNightCity"];
const expected=["weapon_hud.nui","mission_hud.nui","main_menu.nui"];
const catalog=fs.readFileSync(path.join(root,
  "engine/northstarcpp/modules/project_manager/project_catalog.cpp"),"utf8");
const scaffold=catalog.match(/R"NSUI\(([\s\S]*?)\)NSUI"/);
assert.ok(scaffold,"Project Manager must scaffold an asset");
const definition=nui.decode(scaffold[1]);
assert.equal(definition.schema,"northstar.ui/2");
assert.match(catalog,/ui\.default/);
for(let i=0;i<files.length;i++){
  const dir=path.join(root,"Projects",files[i]);
  const manifest=JSON.parse(fs.readFileSync(
      path.join(dir,"northstarcpp.project.json"),"utf8"));
  assert.equal(manifest.ui.default,"ui/"+expected[i]);
  const project=path.join(dir,"assets","ui",expected[i]);
  const shared=path.join(root,"sharedAssets","ui",expected[i]);
  assert.ok(fs.existsSync(project),"Missing project UI: "+files[i]);
  assert.ok(fs.existsSync(shared),"Missing common UI: "+files[i]);
  const data=nui.decode(fs.readFileSync(project,"utf8"));
  assert.ok(data.nodes.length>0);
  const js=fs.readFileSync(path.join(dir,"scripts/main.js"),"utf8");
  assert.doesNotMatch(js,/UI\.(?:mount|render|createRoot)\s*\(/);
  assert.match(js,/UI\.loadAsset\(/);
}
const template=fs.readFileSync(path.join(root,
  "tools/templates/abby_pistol_demo/scripts/main.js"),"utf8");
assert.match(template,/UI\.loadAsset\(/);
assert.doesNotMatch(template,/UI\.mount\(/);
console.log("UI_PROJECT_INVARIANTS_PASS: 3 projects, Project Manager, template");
