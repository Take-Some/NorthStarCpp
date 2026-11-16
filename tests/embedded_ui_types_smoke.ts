// A compile-only sample of public @northstar/engine SDK types.
import {
    UI, Scene, Player, Input, Keys, Events, Script, type UiEvent
} from "@northstar/engine";

const screen = UI.createRoot("health");
screen.group("panel",{width:320,height:160}, panel=>{
    panel.text("caption","Status",{height:28});
    panel.button("use","USE",{height:30});
});
screen.onClick("use",(event: UiEvent)=>{
    if(event.name==="click" && event.value===true){
        const pawn=Player.requirePawn();
        console.log(pawn.position.x);
    }
}).mount();

const world=UI.createSurface("2",{
    layer:"advert",width:256,height:128,worldWidth:2,worldHeight:1
});
world.group("panel",{width:256,height:128},panel=>{
    panel.text("heading","NORTHSTAR",{height:30});
}).mount();

const beacon=Scene.requireByName("Beacon");
const position=Input.normalized2D(
    Input.axis(Keys.D,Keys.A),Input.axis(Keys.W,Keys.S));
console.log(beacon.position.x,position.x);
Events.on("stop",()=>{screen.dispose();world.dispose();});
class Gameplay extends Script { override update(dt:number): void {if(dt>0)screen.setText("caption","Live");} }
new Gameplay().install();
