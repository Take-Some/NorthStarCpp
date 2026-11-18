#!/usr/bin/env node
"use strict";
const assert=require("node:assert/strict");
const path=require("node:path");
const {spawn}=require("node:child_process");
const readline=require("node:readline");
const server=spawn(process.execPath,[path.join(__dirname,"ts_language_service.cjs")],{
    stdio:["pipe","pipe","pipe"],windowsHide:true
});
const pending=new Map();
let seq=0;
const lines=readline.createInterface({input:server.stdout});
lines.on("line",line=>{
    const reply=JSON.parse(line);
    const wait=pending.get(reply.id);
    if(!wait)return;
    pending.delete(reply.id);wait(reply);
});
let errors="";
server.stderr.on("data",data=>errors+=data);
function request(method,text,language,prefix,extra={}) {
    return new Promise((resolve,reject)=>{
        const id=++seq;
        pending.set(id,resolve);
        server.stdin.write(JSON.stringify({
            id,method,text,language,prefix,position:text.length,...extra
        })+"\n",err=>err&&reject(err));
    });
}
const complete=(text,language,prefix)=>request("complete",text,language,prefix);
function applyEdits(source,edits) {
    let result=source;
    for(const e of edits)result=result.slice(0,e.start)+e.text+
                                    result.slice(e.start+e.length);
    return result;
}
async function run() {
    let code='import { UI } from "@northstar/engine";\n'+
             'const menu=UI.createRoot("main");\nmenu.re';
    let result=await complete(code,"typescript","re");
    assert.equal(result.ok,true,JSON.stringify(result));
    assert.ok(result.isMemberCompletion);
    assert.ok(result.items.some(e=>e.name==="render"&&
              e.detail.includes("render")),JSON.stringify(result.items));
    assert.ok(!result.items.some(e=>e.name==="onClick"),
              "Prefix must filter unrelated entries");

    code='import { type UiDeclaration } from "@northstar/engine";\n'+
         'const item:UiDeclaration = {id:"header",type:"text", ch';
    result=await complete(code,"typescript","ch");
    assert.equal(result.ok,true,JSON.stringify(result));
    assert.ok(result.items.some(x=>x.name==="children"),JSON.stringify(result.items));

    code='import { UI } from "@northstar/engine";\n'+
         'const hud=UI.createRoot("hud");\nhud.setT';
    result=await complete(code,"javascript","setT");
    assert.equal(result.ok,true,JSON.stringify(result));
    assert.ok(result.items.some(x=>x.name==="setText"),JSON.stringify(result.items));

    code='import { type UiDeclaration } from "@northstar/engine";\n'+
         'const view:UiDeclaration = {id:"panel",type:"panel",props:{w';
    result=await complete(code,"typescript","w");
    assert.equal(result.ok,true,JSON.stringify(result));
    assert.ok(result.items.some(x=>x.name==="width"),
              "Typed UiProperties must suggest width");
    // Verify source syntax, absent methods, wrong argument types and
    // incorrect assignments all produce actionable TypeScript diagnostics.
    const invalid='import {UI} from "@northstar/engine";\n'+
        'const menu=UI.createRoot("menu");\n'+
        'menu.rnder({id:"root",type:"panel"});\n'+
        'menu.setText("title",42);\n'+
        'const numeric: number="wrong";\n';
    result=await request("diagnostics",invalid,"typescript","");
    assert.equal(result.ok,true,JSON.stringify(result));
    const codes=new Set(result.items.map(e=>e.code));
    assert.ok(codes.has(2551),"unknown SDK method must be flagged");
    assert.ok(codes.has(2345),"wrong argument type must be flagged");
    assert.ok(codes.has(2322),"assignment mismatch must be flagged");
    assert.ok(result.items.every(e=>e.line>=1 && e.column>=1));
    const missingBrace="function square(x:number) {return x*x;\n";
    result=await request("diagnostics",missingBrace,"typescript","");
    assert.ok(result.items.some(e=>e.code===1005),
              "Syntax error must be flagged");
    const typo='import {UI} from "@northstar/engine";\n'+
        'const menu=UI.createRoot("menu");\n'+
        'menu.rnder({id:"root",type:"panel"});\n';
    result=await request("codeActions",typo,"typescript","",{
        position:typo.indexOf("rnder")
    });
    assert.equal(result.ok,true,JSON.stringify(result));
    const fix=result.items.find(x=>x.title.includes("render"));
    assert.ok(fix?.edits?.length,"TypeScript must offer safe spelling fix");
    const corrected=applyEdits(typo,fix.edits);
    assert.ok(corrected.includes("menu.render("));
    result=await request("diagnostics",corrected,"typescript","");
    assert.ok(!result.items.some(e=>e.code===2551));
    const original="const count = 1;\nconsole.log(count);\n"+
                   "const note = 'count';\n";
    result=await request("rename",original,"typescript","",{
        position:original.indexOf("count"),newName:"total"
    });
    assert.equal(result.ok,true,JSON.stringify(result));
    assert.equal(result.items.length,2);
    const renamed=applyEdits(original,result.items);
    assert.ok(renamed.includes("console.log(total)"));
    assert.ok(renamed.includes("const note = 'count'"),
              "Rename cannot modify unrelated string literals");
    result=await request("format","const obj={a:1,b:2};\n","typescript","");
    assert.equal(result.ok,true);
    assert.ok(result.items.length>0,"format action should propose edits");
    const invalidRename=await request("rename",original,"typescript","",{
        position:original.indexOf("count"),newName:"break"
    });
    assert.equal(invalidRename.ok,false,"reserved keywords must not rename");
    console.log("TypeScript LS diagnostics/actions/rename/format: PASS");
    console.log("TypeScript LS: PASS (typed member, nested properties, JS inference, signature metadata)");
    server.stdin.end();
}
run().catch(error=>{
    console.error(error.stack,errors);
    server.kill();process.exitCode=1;
});
const limit=setTimeout(()=>{
    console.error("TypeScript language service timeout",errors);
    server.kill();process.exitCode=1;
},22000);
server.on("exit",()=>clearTimeout(limit));
