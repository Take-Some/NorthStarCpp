// Test-only bridge mirrors the native XML-v3-to-descriptor compiler.
// Production Player uses engine/northstarcpp/modules/shared/ui_xml_codec.hpp.
const path=require("node:path");
const fs=require("node:fs");
const {spawnSync}=require("node:child_process");
const ROOT=path.resolve(__dirname,"../..");
const CLI=path.join(ROOT,"tools/dev/nui_xml.py");
function processEnvPython(){return process.env.NORTHSTAR_PYTHON ||
    (process.platform==="win32"?"python":"python3");}
function run(action,data) {
    const args=[CLI,action];
    let process=spawnSync(processEnvPython(),args,{
        cwd:ROOT,input:data,encoding:"utf8",maxBuffer:2*1024*1024});
    if(process.error && process.error.code==="ENOENT")
        process=spawnSync("python3",args,{
            cwd:ROOT,input:data,encoding:"utf8",maxBuffer:2*1024*1024});
    if(process.error)throw process.error;
    if(process.status!==0)
        throw new Error("UI XML "+action+": "+process.stderr);
    return process.stdout;
}
function compile(source){return run("to-json-stdin",source);}
function decode(source){return JSON.parse(compile(source));}
function encode(descriptor){return run("from-json-stdin",JSON.stringify(descriptor));}
function load(filename){return compile(fs.readFileSync(filename,"utf8"));}
module.exports={compile,decode,encode,load};
