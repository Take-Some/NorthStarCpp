// Compile-only public API contract for TypeScript editor completion.
// TypeScript resolves @northstar/engine via the embedded SDK declarations.
import {
    UI, Events, type UiDeclaration, type UiDocument,
    type UiEvent, type UiEventName, type UIRoot, type UiValue
} from "@northstar/engine";

const onStart=(event:UiEvent):void=>{
    if(event.name==="click" && event.value===true)
        console.log("start");
};
const title:UiValue="MAIN MENU";
const component=(id:string,handler:(event:UiEvent)=>void):UiDeclaration=>({
    id,type:"button",caption:"START",props:{width:180,height:42},
    events:{click:handler}
});
const document:UiDocument={nodes:[{
    id:"card",type:"panel",props:{x:20,y:20,padding:12},
    children:[
        {id:"heading",type:"text",text:String(title),
         props:{font_scale:2}},
        component("start",onStart)
    ]
}]};

const menu:UIRoot=UI.createRoot("menu");
menu.render(document)
    .onClick("start",event=>{
        if(event.value===true) console.log("imperative callback");
    })
    .mount();
menu.render(document,{preserveRuntimeOverrides:true});

const eventType:UiEventName="pointer";
menu.on("start",eventType,event=>console.log(event.value));
Events.on("stop",()=>menu.dispose());
