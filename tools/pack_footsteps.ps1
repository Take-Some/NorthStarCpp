# NorthStar .nac is an audio-kind NEF8 ListFile, not standalone NAC1.
# Rebuilds obsolete NAC1 packs transactionally, with backup and PCM validation.
param(
    [string]$SourceRoot = (Join-Path $PSScriptRoot '..\..\Shared\Source\audio\footsteps'),
    [string]$OutputRoot = (Join-Path $PSScriptRoot '..\sharedAssets\audio\footsteps'),
    [string]$Packer = (Join-Path $PSScriptRoot '..\out\project-manager-qt\northstarcpp_nac_pack.exe')
)
$ErrorActionPreference='Stop'
$engineRoot=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$stageRoot=Join-Path $engineRoot 'out\nac-nef8-staging\footsteps'
$backupRoot=Join-Path $engineRoot 'out\nac1-backup\footsteps'
$verify=Join-Path $PSScriptRoot 'verify_nac_import.py'
if(-not (Test-Path -LiteralPath $SourceRoot -PathType Container)){throw "Missing WAV source: $SourceRoot"}
if(-not (Test-Path -LiteralPath $Packer -PathType Leaf)){throw "Build northstarcpp_nac_pack first"}
$surfaces=@(Get-ChildItem -LiteralPath $SourceRoot -Directory | Sort-Object Name)
if($surfaces.Count -eq 0){throw 'No footsteps surfaces found'}
New-Item -ItemType Directory -Force -Path $OutputRoot,$stageRoot,$backupRoot | Out-Null
$total=0
foreach($surface in $surfaces){
    $name=$surface.Name
    $dest=Join-Path $OutputRoot ($name+'.nac')
    $backup=Join-Path $backupRoot ($name+'.nac')
    $stage=Join-Path $stageRoot ($name+'.nac')
    $wav=@(Get-ChildItem -LiteralPath $surface.FullName -Filter '*.wav' -Recurse -File)
    if($wav.Count -eq 0){throw "Empty surface folder: $name"}
    if(Test-Path -LiteralPath $dest){
        $stream=[IO.File]::OpenRead($dest)
        try {
            if($stream.Length -lt 4){throw "Truncated NAC file: $dest"}
            $header=New-Object byte[] 4
            [void]$stream.Read($header,0,4)
            $magic=[Text.Encoding]::ASCII.GetString($header)
        } finally {$stream.Dispose()}
        if($magic -eq 'NEF8') {
            & $Packer verify $dest
            if($LASTEXITCODE){throw "Invalid NEF8 NAC: $name"}
            & python $verify $surface.FullName $dest
            if($LASTEXITCODE){throw "Source PCM changed for NAC: $name"}
            $total+=$wav.Count
            continue
        }
        if($magic -ne 'NAC1'){throw "Unknown NAC envelope: $name ($magic)"}
        if(Test-Path $backup){throw "Legacy backup already exists: $backup"}
    }
    if(Test-Path $stage){throw "Previous stage already exists: $stage"}
    & $Packer pack $surface.FullName $stage
    if($LASTEXITCODE){throw "NEF8 pack failed: $name"}
    & $Packer verify $stage
    if($LASTEXITCODE){throw "NEF8 verification failed: $name"}
    & python $verify $surface.FullName $stage
    if($LASTEXITCODE){throw "Lossless PCM verification failed: $name"}
    if(Test-Path -LiteralPath $dest) {
        # Atomic swap on same volume. Original NAC1 kept in out/ for rollback.
        [IO.File]::Replace($stage,$dest,$backup)
        Write-Output "MIGRATED NAC1->NEF8: $name"
    } else {
        [IO.File]::Move($stage,$dest)
        Write-Output "CREATED NEF8 NAC: $name"
    }
    $total+=$wav.Count
}
Write-Output "NEF8_FOOTSTEPS_OK surfaces=$($surfaces.Count) tracks=$total"
