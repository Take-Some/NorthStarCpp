# Launch a local NorthStarCpp project using official V8 15.8.62 d8.
# DEVELOPMENT USE ONLY. No production V8 SDK or security attestation.
param(
    [Parameter(Mandatory=$true)][string]$Scene,
    [string]$Player,
    [switch]$Warp,
    [switch]$Fps,
    [switch]$CheckScene,
    [string]$Archive
)
$ErrorActionPreference = "Stop"
$Root = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$Runtime = Join-Path $Root "out\v8-official-windows\15.8.62"
$V8Exe = Join-Path $Runtime "bin\d8.exe"
$Plugin = Join-Path $Runtime "sidecar-build\scripting_v8_sidecar.dll"
$Worker = Join-Path $PSScriptRoot "v8_worker.js"
if(-not $Player) {
    $Player = Join-Path $Root "out\qa-fps-dist-20261114\NorthStarCpp-0.4.0-windows-x64-Release-QA\bin\northstarcpp_player.exe"
}
foreach($file in @($V8Exe,$Plugin,$Worker,$Player)) {
    if(-not (Test-Path -LiteralPath $file -PathType Leaf)) {
        throw "Missing V8 dev runtime file: $file"
    }
}
$ProvenancePath = Join-Path $Runtime "sidecar-provenance.json"
if(-not (Test-Path -LiteralPath $ProvenancePath -PathType Leaf)) {
    throw "Official V8 developer provenance manifest not found"
}
$Evidence = Get-Content -LiteralPath $ProvenancePath -Raw | ConvertFrom-Json
$VerifyFiles = @(
    @($V8Exe, $Evidence.d8_sha256),
    @($Plugin, $Evidence.provider_sha256),
    @($Worker, $Evidence.worker_sha256)
)
foreach($Entry in $VerifyFiles) {
    $Hash = (Get-FileHash -LiteralPath $Entry[0] -Algorithm SHA256).Hash.ToLowerInvariant()
    if($Hash -ne $Entry[1]) {
        throw "V8 developer runtime SHA-256 mismatch: $($Entry[0])"
    }
}
$env:NORTHSTARCPP_SCRIPTING_PLUGIN = (Resolve-Path $Plugin).Path
$env:NORTHSTARCPP_V8_D8_EXECUTABLE = (Resolve-Path $V8Exe).Path
$env:NORTHSTARCPP_V8_SIDECAR_WORKER = (Resolve-Path $Worker).Path
$invoke = @()
if($Warp) { $invoke += "--warp" }
if($Fps) { $invoke += "--fps" }
if($CheckScene) { $invoke += "--check-scene" }
if($Archive) {
    if(-not (Test-Path -LiteralPath $Archive -PathType Leaf)) {
        throw "Archive does not exist: $Archive"
    }
    $invoke += @("--archive",(Resolve-Path $Archive).Path,"--scene",$Scene)
} else {
    $gameScene = if([System.IO.Path]::IsPathRooted($Scene)) { $Scene } else { Join-Path $Root $Scene }
    if(-not (Test-Path -LiteralPath $gameScene -PathType Leaf)) {
        throw "Scene does not exist: $gameScene"
    }
    $invoke += @("--scene",(Resolve-Path $gameScene).Path)
}
Write-Host "NorthStarCpp Developer V8 sidecar: 15.8.62 (not a production release)"
Push-Location (Split-Path -Parent $Player)
try {
    & $Player @invoke
    exit $LASTEXITCODE
} finally {
    Pop-Location
}
