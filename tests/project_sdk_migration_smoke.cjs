"use strict";
// Active V8 project sources must use one generated SDK registry and avoid
// private native provider access. Archived .ysc/migration trees are excluded.
const fs=require("node:fs");
const path=require("node:path");
const assert=require("node:assert/strict");
const repo=path.resolve(__dirname,"..");
const api=path.join(repo,"engine/northstarcpp/resources/sharedAssets/api");
const registry=fs.readFileSync(path.join(api,"core/sdk.js"),"utf8");
const exported=new Set([...registry.matchAll(
    /^\s*get\s+([A-Za-z_][A-Za-z_0-9]*)\(\)/gm
)].map(match=>match[1]));
const projects=["AbbyPistolDemo","KaylaNightCity",
    "MovingCitiesPrototype","NorthStarFirstPersonPlatform"];
const imports=/^\s*import\s*\{([^}]+)\}\s*from\s*["']@northstar\/engine["'];?/gm;
const directNative=/\bengine\.(?:scene|physics|vehicle|audio|save|rendering|ui|ecs)\s*\./;
let scripts=0,namedImports=0;
for(const project of projects){
    const folder=path.join(repo,"Projects",project,"assets","scripts");
    assert.ok(fs.statSync(folder).isDirectory(),project+": missing assets/scripts");
    assert.ok(fs.existsSync(path.join(repo,"Projects",project,"jsconfig.json")),
        project+": missing strict project SDK type checking");
    function visit(directory){
        for(const item of fs.readdirSync(directory,{withFileTypes:true})){
            const file=path.join(directory,item.name);
            if(item.isDirectory()){visit(file);continue;}
            if(!item.name.endsWith(".js"))continue;
            scripts++;
            const content=fs.readFileSync(file,"utf8");
            const withoutComments=content.replace(/\/\*[\s\S]*?\*\//g,"")
                .replace(/^\s*\/\/[^\n]*/gm,"");
            assert.ok(!directNative.test(withoutComments),
                project+": direct engine.* capability bypass in "+file);
            for(const match of content.matchAll(imports)){
                namedImports++;
                const names=match[1].split(",").map(n=>n.trim().split(/\s+as\s+/)[0]);
                for(const name of names){
                    if(!name)continue;
                    assert.ok(exported.has(name),project+": SDK export "+name+
                        " is missing from EngineModuleRegistry");
                }
            }
            assert.ok(namedImports>0,project+": missing engine imports");
        }
    }
    visit(folder);
}
for(const [project,template] of [["AbbyPistolDemo","tools/templates/abby_pistol_demo/scripts/main.js"],["MovingCitiesPrototype","tools/templates/moving_cities/main.js"]]) {
  const a=fs.readFileSync(path.join(repo,"Projects",project,"assets/scripts/main.js"),"utf8").replace(/\r\n/g,"\n");
  const b=fs.readFileSync(path.join(repo,template),"utf8").replace(/\r\n/g,"\n");
  assert.equal(a,b,project+": generated script must match its canonical SDK-compatible template");
}
assert.equal(projects.length,4);
assert.ok(scripts>=10);
console.log("PASS 4 active projects: "+scripts+" script files, "+
    namedImports+" canonical imports, zero direct native-provider bypasses");
