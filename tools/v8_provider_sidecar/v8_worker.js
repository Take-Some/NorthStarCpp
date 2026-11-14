// NorthStarCpp dev-only V8 15.8 sidecar. Uses official d8.exe shell.
// Transport: ASCII hex newline frames, no native libraries shared with Qt.
// The d8 shell is not a production security boundary.
"use strict";
(() => {
  const protocolPrint=print;
  const protocolRead=readline;
  const hex="0123456789abcdef";
  const hexLookup=new Int8Array(128).fill(-1);
  for(let i=0;i<16;i++) {
    hexLookup[hex.charCodeAt(i)]=i;
    if(i>=10)hexLookup[hex.toUpperCase().charCodeAt(i)]=i;
  }
  // Decode via an array of percent-encoded bytes instead of repeated
  // immutable-string concatenation. Each hex pair is validated once.
  function fromHex(value) {
    if(typeof value!=="string" || (value.length&1) || value.length>16777216)
      throw new Error("Invalid IPC frame");
    const pieces=new Array(value.length>>1);
    for(let i=0,j=0;i<value.length;i+=2,j++) {
      const a=value.charCodeAt(i),b=value.charCodeAt(i+1);
      if(a>=128||b>=128||hexLookup[a]<0||hexLookup[b]<0)
        throw new Error("Malformed IPC hex");
      pieces[j]="%"+hex[hexLookup[a]]+hex[hexLookup[b]];
    }
    return decodeURIComponent(pieces.join(""));
  }
  function toHex(value) {
    // V8 optimizes the string-concatenation fast path better than array
    // building for this direction (measured on 15.8.62). Keep it here.
    const utf8=encodeURIComponent(String(value));
    let output="";
    for(let i=0;i<utf8.length;i++) {
      if(utf8[i]==="%") {
        output+=utf8.substring(i+1,i+3).toLowerCase();
        i+=2;
      } else {
        const b=utf8.charCodeAt(i);
        output+=hex[b>>4]+hex[b&15];
      }
    }
    return output;
  }
  function nativeCall(capability,method,args) {
    const json=JSON.stringify(args);
    if(typeof json!=="string" || json.length>4194304)
      throw new Error("Invalid engine callback arguments");
    protocolPrint("CALL|"+toHex(capability)+"|"+toHex(method)+"|"+toHex(json));
    const response=protocolRead();
    if(typeof response!=="string")throw new Error("Native bridge disconnected");
    const fields=response.split("|");
    if(fields.length!==3 || fields[0]!=="ANS")
      throw new Error("Malformed native callback response");
    const value=fromHex(fields[2]);
    if(fields[1]==="0")throw new Error(value);
    if(fields[1]!=="1")throw new Error("Invalid native callback status");
    return JSON.parse(value);
  }
  function initRealmApi() {
    "use strict";
    const native=Realm.shared.call;
    const subscriptions=new Map();
    const namespaces=new Map();
    // Cache capability proxies and their bound methods: gameplay often calls
    // engine.input/engine.scene many times per frame.
    const exposed=(ns)=>{
      let proxy=namespaces.get(ns);
      if(proxy)return proxy;
      const methods=new Map();
      proxy=new Proxy(Object.create(null),{
        get(_target,method) {
          if(typeof method!=="string" || method==="then")return undefined;
          let fn=methods.get(method);
          if(!fn) {
            fn=(...args)=>native(ns,method,args);
            methods.set(method,fn);
          }
          return fn;
        }
      });
      namespaces.set(ns,proxy);
      return proxy;
    };
    const host={
      call(ns,method,...args){return native(ns,method,args);},
      api(ns){
        if(typeof ns!=="string"||!ns.startsWith("engine."))
          throw new TypeError("Expected engine namespace");
        return exposed(ns);
      },
      on(name,callback){
        if(typeof name!=="string"||typeof callback!=="function")
          throw new TypeError("Invalid event listener");
        let handlers=subscriptions.get(name);
        if(!handlers)subscriptions.set(name,handlers=new Set());
        handlers.add(callback);
        return ()=>handlers.delete(callback);
      },
      off(name,callback) {return subscriptions.get(name)?.delete(callback)??false;},
      emit(name,payload) {
        let count=0;
        for(const fn of [...(subscriptions.get(name)??[])]) {fn(payload);count++;}
        return count;
      }
    };
    const engine=new Proxy(Object.freeze(host),{
      get(target,key) {
        if(Object.prototype.hasOwnProperty.call(target,key))return target[key];
        if(typeof key!=="string"||key==="then")return undefined;
        return exposed("engine."+key);
      }
    });
    Object.defineProperty(globalThis,"engine",{
      value:engine,writable:false,enumerable:false,configurable:false
    });
    // Access the registered callbacks from the trusted d8 transport realm
    // without recompiling the same engine.emit wrapper every frame.
    Realm.shared.emit=(event,payload)=>engine.emit(event,payload);
    // Dev-shell globals must not accidentally become public engine APIs.
    // This reduces accidental exposure but does NOT harden d8 for untrusted JS.
    for(const key of ["Realm","os","read","readbuffer","load","readline",
                      "print","printErr","Worker","d8","quit","write",
                      "writeFile","setTimeout","setInterval","console"]) {
      try {globalThis[key]=undefined;} catch(_error) {}
    }
  }
  const initRealmSource="("+initRealmApi.toString()+")()";
  // Exact ScriptRuntime::emit_event source shape. These are JSON strings,
  // not arbitrary JS expressions; do not evaluate captured text as source.
  const engineEventFrame=/^engine\.emit\(("(?:[^"\\]|\\.)*"),JSON\.parse\(("(?:[^"\\]|\\.)*")\)\)$/;
  let realm=-1;
  function reset() {
    if(realm>=0)Realm.dispose(realm);
    realm=Realm.create();
    Realm.shared={call:nativeCall};
    Realm.eval(realm,initRealmSource);
    const exposed=Realm.eval(realm,
      '["Realm","os","read","load","readline","print","Worker"].filter(k=>typeof globalThis[k]!=="undefined").join(",")');
    if(exposed)throw new Error("Unsafe d8 globals: "+exposed);
  }
  try {
    reset();
    protocolPrint("READY|"+version());
    while(true) {
      const line=protocolRead();
      if(line==="EXIT" || typeof line!=="string")break;
      try {
        if(line==="RESET") {reset();protocolPrint("OK|6e756c6c");continue;}
        const parts=line.split("|");
        if(parts.length!==3||parts[0]!=="E")
          throw new Error("Malformed eval frame");
        const source=fromHex(parts[1]);
        const origin=fromHex(parts[2]);
        if(source.includes("\0")||source.length>1048576||origin.length>1024)
          throw new Error("Invalid JS source or origin length");
        const event=(origin==="<engine-event>")?
           engineEventFrame.exec(source):null;
        // Specialized, data-only event bridge; regular scripts still use
        // Realm.eval and retain their lexical/global evaluation semantics.
        const result=event?
           Realm.shared.emit(JSON.parse(event[1]),JSON.parse(JSON.parse(event[2]))):
           Realm.eval(realm,source+"\n//# sourceURL="+
             origin.replace(/[\r\n]/g,"_"));
        const json=result===undefined?"null":JSON.stringify(result);
        if(typeof json!=="string"||json.length>4194304)
          throw new Error("Cannot serialize JS result");
        protocolPrint("OK|"+toHex(json));
      } catch(error) {
        protocolPrint("ERR|"+toHex(String(error&&error.stack||error)));
      }
    }
  } catch(error) {
    protocolPrint("FATAL|"+toHex(String(error&&error.stack||error)));
  }
})();
