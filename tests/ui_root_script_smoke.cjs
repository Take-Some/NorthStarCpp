// Shared engine UI root contract. Executes the actual default root script
// against the actual class from bootstrap.js without mocking UI behavior.
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");
const shared = path.join(__dirname, "..", "engine", "northstarcpp", "resources", "sharedAssets", "api");
const bootstrap = fs.readFileSync(path.join(shared, "northstar", "bootstrap.js"), "utf8");
const rootScript = fs.readFileSync(path.join(shared, "ui", "UI.js"), "utf8");
const a=bootstrap.indexOf("    class UI {");
const b=bootstrap.indexOf("    class Logger {",a);
assert.ok(a>=0&&b>a);
const documents=[];
const sandbox={
    engine:{
        ui:{
            documents:{apply: doc=>{documents.push(doc);return null;}},
            viewport:{setTitle:()=>null}
        }
    },
    NorthStar:{modules:{engine:{}}}
};
const context=vm.createContext(sandbox);
const UI=vm.runInContext(bootstrap.slice(a,b)+"\nUI;",context);
sandbox.NorthStar.modules.engine.UI=UI;
vm.runInContext(rootScript,context,{filename:"scripts/ui/UI.js"});

const mission=UI.createRoot("quest");
mission.panel("box",{
    width:500,height:160,background:"#111a28dd",padding:8
}).text("title","MISSION ACTIVE",{height:32,font_scale:2},"box")
  .progress("progress",0.25,{width:220,height:12},"box")
  .mount();

assert.equal(documents.at(-1).target,"screen");
assert.equal(documents.at(-1).nodes.length,4);
assert.equal(documents.at(-1).nodes[1].id,"quest.title");
assert.equal(documents.at(-1).nodes[3].id,"quest.progress.fill");
assert.equal(documents.at(-1).nodes[3].props.width.value,55);
assert.equal(mission.mounted,true);

const health=UI.createRoot("health");
health.panel("frame",{width:260,height:45})
      .text("label","HP 100",{height:32},"frame")
      .mount();
assert.equal(documents.at(-1).nodes.length,6);
mission.batch(ui=>{
    ui.setText("title","MISSION COMPLETE");
    ui.setProgress("progress",1);
});
assert.equal(documents.at(-1).nodes.length,6);
const title=documents.at(-1).nodes.find(n=>n.id==="quest.title");
assert.equal(title.props.text.value,"MISSION COMPLETE");
assert.equal(documents.at(-1).nodes.find(n=>n.id==="quest.progress.fill").props.width.value,220);

mission.remove("progress");
assert.equal(documents.at(-1).nodes.filter(n=>n.id.startsWith("quest.progress")).length,0);
health.unmount();
assert.equal(documents.at(-1).nodes.filter(n=>n.id.startsWith("health.")).length,0);
mission.dispose();
assert.equal(documents.at(-1).nodes.length,0);
assert.equal(mission.size,0);
assert.throws(()=>UI.createRoot("bad name!"),/layer must be a valid identifier/);
assert.throws(()=>UI.registerRoot(class Duplicate {}),/already been registered/);

console.log("Shared UI root script: PASS (fluent elements, progress, lifecycle, layers, validation)");
