"""Native screenshot probe in temporary AbbyPistolDemo copy."""
from pathlib import Path
import os,subprocess,tempfile,shutil,time
root=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="northstar-pose-") as directory:
 base=Path(directory);game=base/"game";shutil.copytree(root/"Projects/AbbyPistolDemo",game)
 s=game/"assets/scripts/main.js"
 arms_only=os.environ.get("POSE_VIEWMODE")=="arms"
 s.write_text(s.read_text("utf8")+"""
const A=NorthStar.modules.engine;
const original=A.Input.mouseDown;
A.Input.mouseDown=function(b){return b===1 || original.call(this,b)};
let qaFrames=0;
A.Events.on("update",()=>{
  qaFrames++;
  if(qaFrames===40 && __ARMS__) {
    const actor=A.Scene.findByName("SkeletalMesh");
    if(actor)A.Scene.setVisualMode(actor.index,"first_person_arms",
      {forward:.52,drop:.115,right:.063,armPitch:0,ads:true});
  }
  if(qaFrames===6000)engine.session.quit();
});
""".replace("__ARMS__","true" if arms_only else "false"),encoding="utf8")
 out=root/"out/qa_pistol_pose";out.mkdir(parents=True,exist_ok=True)
 env=os.environ.copy()
 env.update({"LOCALAPPDATA":str(base/"local"),"NORTHSTARCPP_LOG_DIRECTORY":str(base/"logs"),
  "NORTHSTARCPP_RENDER_PROVIDER":"d3d12","NORTHSTARCPP_PHYSICS_PROVIDER":"jolt"})
 (base/"local").mkdir();(base/"logs").mkdir()
 command=[str(root/"out/build/northstarcpp_player.exe"),"--warp","--explore","--scene",
  str((game/"main.nscn").resolve()),"--shared-assets",str((root/"engine/northstarcpp/resources/sharedAssets").resolve())]
 with (out/"player_stdout.log").open("wb") as stdout,(out/"player_stderr.log").open("wb") as stderr:
  proc=subprocess.Popen(command,cwd=root/"out/build",env=env,stdout=stdout,stderr=stderr)
  try:
   time.sleep(4)
   ps=r"""
Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Windows.Forms
$r=[System.Windows.Forms.Screen]::PrimaryScreen.Bounds
$b=New-Object System.Drawing.Bitmap($r.Width,$r.Height)
$g=[System.Drawing.Graphics]::FromImage($b)
$g.CopyFromScreen($r.Left,$r.Top,0,0,$r.Size)
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class QACapture {
  [DllImport("user32.dll")] public static extern bool PrintWindow(
    IntPtr hWnd, IntPtr hdc, uint flags);
}
'@
$player=Get-Process -Name northstarcpp_player -ErrorAction SilentlyContinue |
  Where-Object { $_.MainWindowHandle -ne [IntPtr]::Zero } | Select-Object -First 1
if($false -and $player){
  $dc=$g.GetHdc()
  $ok=[QACapture]::PrintWindow($player.MainWindowHandle,$dc,0)
  $g.ReleaseHdc($dc)
  Write-Host "PrintWindow=$ok hwnd=$($player.MainWindowHandle)"
}
$t=New-Object System.Drawing.Bitmap(840,472)
$h=[System.Drawing.Graphics]::FromImage($t)
$h.DrawImage($b,0,0,840,472)
$t.Save($args[0],[System.Drawing.Imaging.ImageFormat]::Jpeg)
$h.Dispose();$g.Dispose();$t.Dispose();$b.Dispose()
Write-Host "CAPTURED $($r.Width)x$($r.Height)"
"""
   ps=ps.replace("$args[0]","'"+str(out/("ads_arms.jpg" if arms_only else "ads_screenshot.jpg"))+"'")
   res=subprocess.run(["powershell","-NoProfile","-Command",ps],
     capture_output=True,timeout=20)
   print("capture:",res.returncode,res.stdout.decode("utf8","replace"),
      res.stderr.decode("utf8","replace"))
   print("player_running_at_capture",proc.poll() is None)
  finally:
   if proc.poll() is None:proc.terminate();proc.wait(timeout=5)
