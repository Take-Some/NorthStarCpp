#!/usr/bin/env node
"use strict";
// NorthStar TypeScript Language Service. JSON-line requests, no eval of source.
// TypeScript/Qt positions are UTF-16. All edits are restricted to the current
// in-memory document; the service NEVER writes user files.
const ts=require("typescript");
const fs=require("node:fs");
const path=require("node:path");
const readline=require("node:readline");

const sdk=process.env.NORTHSTAR_TS_DECLARATIONS ||
    path.resolve(__dirname,"../../engine/northstarcpp/resources/sharedAssets/api/types/northstar-engine.d.ts");
const key=name=>path.normalize(name).toLowerCase();
const tsName=path.resolve(__dirname,"northstar_editor_document.ts");
const jsName=path.resolve(__dirname,"northstar_editor_document.js");
const typesName=path.resolve(sdk);
const files=new Map([[key(typesName),{
    text:fs.readFileSync(typesName,"utf8"),version:1
}]]);
const options={
    target:ts.ScriptTarget.ES2022,
    module:ts.ModuleKind.ESNext,
    moduleResolution:ts.ModuleResolutionKind.Bundler,
    allowJs:true,checkJs:true,strict:true,noEmit:true,skipLibCheck:false,
    types:[],lib:["lib.es2022.d.ts","lib.dom.d.ts"]
};
let currentFile=tsName;
let service=null;
function languageService(){
    if(service)return service;
    const host={
        getScriptFileNames:()=>[typesName,currentFile],
        getScriptVersion:filename=>String(files.get(key(filename))?.version||0),
        getScriptSnapshot(filename){
            const doc=files.get(key(filename));
            if(doc)return ts.ScriptSnapshot.fromString(doc.text);
            const content=ts.sys.readFile(filename);
            return content===undefined?undefined:ts.ScriptSnapshot.fromString(content);
        },
        getCurrentDirectory:()=>process.cwd(),
        getCompilationSettings:()=>options,
        getDefaultLibFileName:ts.getDefaultLibFilePath,
        fileExists:filename=>files.has(key(filename))||ts.sys.fileExists(filename),
        readFile:filename=>files.get(key(filename))?.text??ts.sys.readFile(filename),
        readDirectory:ts.sys.readDirectory,
        directoryExists:ts.sys.directoryExists,
        realpath:ts.sys.realpath,
        useCaseSensitiveFileNames:()=>ts.sys.useCaseSensitiveFileNames,
        getNewLine:()=>ts.sys.newLine
    };
    service=ts.createLanguageService(host,ts.createDocumentRegistry());
    return service;
}
function prepare(input){
    const text=String(input.text??"");
    if(text.length>800000)throw Error("Document too large for realtime analysis");
    const position=input.position===undefined?0:Number(input.position);
    if(!Number.isSafeInteger(position)||position<0||position>text.length)
        throw Error("Invalid UTF-16 source offset");
    const next=input.language==="javascript"?jsName:tsName;
    if(next!==currentFile){
        files.delete(key(currentFile));currentFile=next;
        service?.dispose();service=null;
    }
    const prev=files.get(key(currentFile));
    if(!prev||prev.text!==text)
        files.set(key(currentFile),{text,version:(prev?.version||0)+1});
    return {svc:languageService(),position,text};
}
function complete(input){
    const {svc,position}=prepare(input);
    const result=svc.getCompletionsAtPosition(currentFile,position,{
        includeCompletionsForModuleExports:false,
        includeInsertTextCompletions:true,
        includeCompletionsWithSnippetText:true
    });
    const prefix=String(input.prefix||"").toLowerCase();
    const candidates=(result?.entries||[]).filter(entry=>
        !prefix||entry.name.toLowerCase().startsWith(prefix)).slice(0,90);
    const items=candidates.map(entry=>{
        let detail="",documentation="";
        try {
            const data=svc.getCompletionEntryDetails(
                currentFile,position,entry.name,{},entry.source,
                {includeCompletionsForModuleExports:false},entry.data);
            detail=ts.displayPartsToString(data?.displayParts||[]);
            documentation=ts.displayPartsToString(data?.documentation||[]);
        }catch(_){} // metadata must not prevent completion
        return {
            name:entry.name,kind:entry.kind,
            insertText:entry.insertText||entry.name,
            detail:detail.slice(0,260),documentation:documentation.slice(0,400),
            replacement:entry.replacementSpan?{
                start:entry.replacementSpan.start,
                length:entry.replacementSpan.length}:null
        };
    });
    let signature=null;
    const help=svc.getSignatureHelpItems(currentFile,position,{
        triggerReason:{kind:"invoked"}});
    if(help?.items?.length){
        const sig=help.items[Math.min(help.selectedItemIndex,help.items.length-1)];
        const fmt=parts=>ts.displayPartsToString(parts);
        signature=fmt(sig.prefixDisplayParts)+
            sig.parameters.map(p=>fmt(p.displayParts))
                .join(fmt(sig.separatorDisplayParts))+fmt(sig.suffixDisplayParts);
    }
    return {items,signature,isMemberCompletion:!!result?.isMemberCompletion};
}
function allDiagnostics(svc){
    return [
        ...svc.getSyntacticDiagnostics(currentFile),
        ...svc.getSemanticDiagnostics(currentFile),
        ...svc.getSuggestionDiagnostics(currentFile)
    ];
}
function diagnostics(input){
    const {svc}=prepare(input);
    const all=allDiagnostics(svc);
    const file=svc.getProgram()?.getSourceFile(currentFile);
    const items=all.slice(0,200).map(d=>{
        const start=Math.max(0,d.start||0);
        const at=(d.file||file)?.getLineAndCharacterOfPosition(start);
        return {
            start,length:Math.max(1,d.length||1),
            line:(at?.line||0)+1,column:(at?.character||0)+1,
            category:ts.DiagnosticCategory[d.category].toLowerCase(),
            code:d.code,
            message:ts.flattenDiagnosticMessageText(d.messageText,"\n")
        };
    });
    return {items,truncated:all.length>items.length};
}
function safeEdits(changes,text){
    const edits=[];
    for(const change of changes||[]){
        if(key(change.fileName)!==key(currentFile))return null;
        for(const e of change.textChanges||[])
            edits.push({start:e.span.start,length:e.span.length,text:e.newText});
    }
    edits.sort((a,b)=>b.start-a.start);
    if(edits.some((e,i)=>!Number.isSafeInteger(e.start) ||
        !Number.isSafeInteger(e.length) || e.start<0 || e.length<0 ||
        e.start+e.length>text.length ||
        (i>0&&e.start+e.length>edits[i-1].start)))return null;
    return edits;
}
function codeActions(input){
    const {svc,position,text}=prepare(input);
    const all=allDiagnostics(svc);
    const d=all.find(x=>(x.start||0)<=position &&
        position<=(x.start||0)+Math.max(1,x.length||1))||
        all.find(x=>Math.abs((x.start||0)-position)<3);
    if(!d)return {items:[]};
    const actions=svc.getCodeFixesAtPosition(currentFile,
        d.start||0,(d.start||0)+(d.length||0),[d.code],
        {indentSize:4,tabSize:4,newLineCharacter:"\n"},{});
    const items=[];
    for(const action of actions){
        const edits=safeEdits(action.changes,text);
        // Never disguise compiler errors by inserting @ts-ignore/nocheck.
        // Quick Fix must repair the actual source rather than silence checks.
        if(edits?.length && !edits.some(e=>
            /@ts-(?:ignore|nocheck|expect-error)/.test(e.text)))
            items.push({title:action.description,code:d.code,edits});
        if(items.length>=25)break;
    }
    // Conservative source repair: only a single missing delimiter at EOF.
    // Verify that the candidate parses with ZERO syntax diagnostics before
    // offering an edit; never invent arbitrary source transformations.
    if(d.code===1005 && (d.start||0)>=text.length) {
        const message=ts.flattenDiagnosticMessageText(d.messageText," ");
        const token=/^'([})\]])' expected/.exec(message)?.[1];
        const syntax=svc.getSyntacticDiagnostics(currentFile);
        if(token && syntax.length===1) {
            const candidate=text+token;
            const source=ts.createSourceFile(currentFile,candidate,
                ts.ScriptTarget.ES2022,true,
                input.language==="javascript"?ts.ScriptKind.JS:ts.ScriptKind.TS);
            if(source.parseDiagnostics.length===0)
                items.unshift({title:"Insert missing closing "+token+
                    " (verified syntax repair)",code:d.code,
                    edits:[{start:text.length,length:0,text:token}]});
        }
    }
    return {items};
}
function rename(input){
    const {svc,position,text}=prepare(input);
    const target=String(input.newName||"");
    const token=ts.stringToToken(target);
    if(!ts.isIdentifierText(target,ts.ScriptTarget.ES2022) ||
       target.length>100 || (token!==undefined &&
       token>=ts.SyntaxKind.FirstKeyword && token<=ts.SyntaxKind.LastKeyword))
        throw Error("Invalid or reserved rename identifier");
    const info=svc.getRenameInfo(currentFile,position,{
        allowRenameOfImportPath:false});
    if(!info.canRename)
        throw Error(info.localizedErrorMessage||"Cannot rename symbol");
    const locations=svc.findRenameLocations(currentFile,position,
        false,false,false)||[];
    if(!locations.length||locations.length>1000||
       locations.some(x=>key(x.fileName)!==key(currentFile)))
        throw Error("Multi-file rename is not supported");
    const edits=locations.map(x=>({
        start:x.textSpan.start,length:x.textSpan.length,text:target
    })).sort((a,b)=>b.start-a.start);
    if(edits.some(x=>x.start<0||x.start+x.length>text.length))
        throw Error("Rename span out of bounds");
    return {symbol:info.displayName,items:edits};
}
function formatDocument(input){
    const {svc,text}=prepare(input);
    const changes=svc.getFormattingEditsForDocument(currentFile,{
        indentSize:4,tabSize:4,convertTabsToSpaces:true,newLineCharacter:"\n",
        insertSpaceAfterCommaDelimiter:true,
        insertSpaceBeforeAndAfterBinaryOperators:true,
        insertSpaceAfterKeywordsInControlFlowStatements:true
    });
    const items=changes.map(x=>({start:x.span.start,
        length:x.span.length,text:x.newText})).sort((a,b)=>b.start-a.start);
    if(items.length>4000||items.some(x=>x.start+x.length>text.length))
        throw Error("Formatting is too large");
    return {items};
}
const methods={complete,diagnostics,codeActions,rename,format:formatDocument};
const input=readline.createInterface({input:process.stdin,crlfDelay:Infinity});
input.on("line",line=>{
    let id=null;
    try {
        const msg=JSON.parse(line);
        id=msg.id;
        if(!Object.hasOwn(methods,msg.method))
            throw Error("Unknown TypeScript service method");
        const reply=methods[msg.method](msg);
        process.stdout.write(JSON.stringify({id,ok:true,...reply})+"\n");
    }catch(err){
        process.stdout.write(JSON.stringify({
            id,ok:false,error:String(err?.message||err)})+"\n");
    }
});
