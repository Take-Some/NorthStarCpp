# Inspect local Cyberpunk 2077 archives with an installed WolvenKit CLI.
# Extraction is for local comparison; game assets are NOT packaged with NorthStar.
[CmdletBinding()]
param(
  [string]$GamePath = 'C:\Games\Cyberpunk 2077',
  [string]$CliPath = '',
  [string]$OutputRoot = '',
  [switch]$AllBasegameArchives,
  [switch]$DryRun
)
$ErrorActionPreference = 'Stop'
if(-not (Test-Path -LiteralPath $GamePath -PathType Container)){
  throw "Cyberpunk installation not found: $GamePath"
}
if(-not $OutputRoot){
  $OutputRoot = Join-Path (Split-Path $PSScriptRoot -Parent) 'out\cyberpunk_weapon_fx_extract'
}
$archiveDir = Join-Path $GamePath 'archive\pc\content'
if(-not(Test-Path -LiteralPath $archiveDir -PathType Container)){
  throw "Archive directory missing: $archiveDir"
}
$names = @('basegame_1_engine.archive','basegame_4_gamedata.archive',
  'basegame_4_appearance.archive','basegame_3_nightcity.archive')
if($AllBasegameArchives){
  $archives = @(Get-ChildItem -LiteralPath $archiveDir -Filter 'basegame_*.archive' -File)
}else{
  $archives = @($names | ForEach-Object {Get-Item -LiteralPath (Join-Path $archiveDir $_) -ErrorAction SilentlyContinue})
}
$regex = '(?i).*(muzzle|pistol|gunshot|shell|eject|weapon|impact|spark|smoke|vfx|bullet|firearm).*\.(effect|particle)$'
if(-not $DryRun){
  if(-not $CliPath){
    $localCli = Join-Path (Split-Path $PSScriptRoot -Parent) 'out\tools\WolvenKit.Console-8.20.0\WolvenKit.CLI.exe'
    if(Test-Path -LiteralPath $localCli -PathType Leaf){$CliPath=$localCli}
    else{
      $found = Get-Command 'WolvenKit.CLI.exe' -ErrorAction SilentlyContinue
      if($found){$CliPath=$found.Source}
    }
  }
  if(-not $CliPath -or -not(Test-Path -LiteralPath $CliPath -PathType Leaf)){
    throw 'WolvenKit.CLI.exe not found. Install WolvenKit CLI first, then pass -CliPath.'
  }
  $localDotnet=Join-Path (Split-Path $PSScriptRoot -Parent) 'out\tools\dotnet'
  if(Test-Path -LiteralPath (Join-Path $localDotnet 'dotnet.exe')){
    $env:DOTNET_ROOT=(Resolve-Path -LiteralPath $localDotnet).Path
    $env:DOTNET_ROOT_X64=$env:DOTNET_ROOT
    $env:DOTNET_ROLL_FORWARD='Major'
  }
  New-Item -ItemType Directory -Force -Path $OutputRoot | Out-Null
}
foreach($archive in $archives){
  $dest = Join-Path $OutputRoot $archive.BaseName
  $args = @('unbundle','-p',$archive.FullName,'-o',$dest,'-r',$regex)
  Write-Host ('WolvenKit.CLI.exe '+($args | ForEach-Object {'"'+$_+'"'}) -join ' ')
  if($DryRun){continue}
  New-Item -ItemType Directory -Force -Path $dest | Out-Null
  & $CliPath @args
  if($LASTEXITCODE -ne 0){throw "Failed to extract from $($archive.Name): $LASTEXITCODE"}
}
if($DryRun){return}
$assets = @(Get-ChildItem -LiteralPath $OutputRoot -Recurse -File |
  Where-Object {$_.Extension -in @('.effect','.particle')})
$manifest = Join-Path $OutputRoot 'weapon-fx-manifest.csv'
$assets | Select-Object @{Name='RelativePath';Expression={$_.FullName.Substring($OutputRoot.Length+1)}},
  @{Name='Bytes';Expression={$_.Length}},Extension |
  Export-Csv -Path $manifest -NoTypeInformation -Encoding UTF8
Write-Host "Extracted $($assets.Count) candidate resources. Manifest: $manifest"
