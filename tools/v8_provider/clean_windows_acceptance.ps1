#Requires -Version 5.1
<#
Runs INSIDE a separately provisioned, clean Windows 10/11 VM (not on the
development host). No compilers, Python, SDKs, or network are required.
This script produces QA evidence only. Final release approval requires an
independent detached signature over the resulting JSON.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$PackageRoot,
    [Parameter(Mandatory=$true)][string]$OutputReport,
    [Parameter(Mandatory=$true)][string]$SourceProject,
    [Parameter(Mandatory=$true)][string]$ArchiveFile
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$checks = [ordered]@{}
$logs = [ordered]@{}
$package = [IO.Path]::GetFullPath($PackageRoot)
$source = [IO.Path]::GetFullPath($SourceProject)
$archive = [IO.Path]::GetFullPath($ArchiveFile)
$reportPath = [IO.Path]::GetFullPath($OutputReport)
$manifestPath = Join-Path $package 'manifest.json'
$env:PATH = (Join-Path $env:SystemRoot 'System32') + ';' + $env:SystemRoot
Remove-Item Env:QT_PLUGIN_PATH -ErrorAction SilentlyContinue
Remove-Item Env:QT_QPA_PLATFORM_PLUGIN_PATH -ErrorAction SilentlyContinue

function Record-Check([string]$Name,[scriptblock]$Action) {
    try {
        & $Action
        $script:checks[$Name] = $true
    } catch {
        $script:checks[$Name] = $false
        $script:logs[$Name] = $_.Exception.Message
    }
}
function Launch-Checked([string]$Executable,[string[]]$Parameters,[int]$TimeoutSec=45) {
    if (-not (Test-Path -LiteralPath $Executable -PathType Leaf)) {
        throw "Required executable missing: $Executable"
    }
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $Executable
    $psi.WorkingDirectory = Split-Path -Parent $Executable
    $psi.UseShellExecute = $false
    $psi.CreateNoWindow = $true
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $psi.Arguments = (($Parameters | ForEach-Object {
        if ($_ -match '"') { throw 'Unsafe double quote in test parameter' }
        '"' + $_ + '"'
    }) -join ' ')
    $process = New-Object System.Diagnostics.Process
    $process.StartInfo = $psi
    if (-not $process.Start()) { throw "Failed to launch $Executable" }
    $stdout = $process.StandardOutput.ReadToEndAsync()
    $stderr = $process.StandardError.ReadToEndAsync()
    if (-not $process.WaitForExit($TimeoutSec*1000)) {
        $process.Kill()
        throw "Process timeout: $Executable"
    }
    $output = $stdout.GetAwaiter().GetResult() + $stderr.GetAwaiter().GetResult()
    if ($process.ExitCode -ne 0) {
        throw "Process exited with $($process.ExitCode): $($output.Substring([Math]::Max(0,$output.Length-300)))"
    }
    if ($output -match 'Cannot locate asset|Asset not found:|Plugin ABI mismatch|Unhandled project script error') {
        throw "Runtime diagnostics contain an error: $output"
    }
}
function Check-Markers([string]$AppData) {
    $names=@('scripting_start','scripting_update','scripting_stop')
    $expect=@('start','update','stop')
    for ($i=0; $i -lt 3; $i++) {
        $matches = @(Get-ChildItem -LiteralPath $AppData -Filter ($names[$i]+'.nsave') -File -Recurse)
        if ($matches.Count -ne 1) { throw "Expected a single $($names[$i]) marker" }
        $payload = [Text.Encoding]::UTF8.GetString([IO.File]::ReadAllBytes($matches[0].FullName))
        $match = [regex]::Match($payload,'(?s)^NorthStarCpp\.Save 1\r?\n(?<size>\d+)\r?\n[^\r\n]*\r?\n(?<data>.*)$')
        if (-not $match.Success) { throw "Invalid SaveGame format for $($names[$i])" }
        $data = $match.Groups['data'].Value
        if ([Text.Encoding]::UTF8.GetByteCount($data) -ne [int]$match.Groups['size'].Value) {
            throw "Corrupt SaveGame length: $($names[$i])"
        }
        $marker = $data | ConvertFrom-Json
        if ($marker.phase -ne $expect[$i]) { throw "Wrong lifecycle phase: $($names[$i])" }
        if ($names[$i] -eq 'scripting_start' -and $marker.objects -lt 5) {
            throw 'Scene objects were not loaded'
        }
        if ($names[$i] -eq 'scripting_update' -and $marker.frames -lt 4) {
            throw 'Script never reached 4 updates'
        }
    }
}

Record-Check 'independent_virtual_machine' {
    $machine = Get-CimInstance Win32_ComputerSystem
    $model = [string]$machine.Model
    if ($model -notmatch '(?i)Virtual Machine|VMware|VirtualBox|QEMU|KVM|Hyper-V|Parallels') {
        throw 'This environment is not an independently provisioned VM'
    }
}
Record-Check 'developer_toolchains_absent' {
    foreach ($candidate in @(
        'C:\msys64',
        'C:\depot_tools',
        'C:\Program Files\Git',
        'C:\Program Files\CMake',
        'C:\Program Files\Microsoft Visual Studio',
        'C:\Program Files (x86)\Microsoft Visual Studio'
    )) {
        if (Test-Path -LiteralPath $candidate) {
            throw "Developer toolchain detected: $candidate"
        }
    }
}
Record-Check 'manifest_sha256' {
    if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
        throw 'Signed-stage package manifest missing'
    }
    $manifest = Get-Content -LiteralPath $manifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
    if ($manifest.engine -ne 'NorthStarCpp') { throw 'Wrong package identity' }
    $actual = @(Get-ChildItem -LiteralPath $package -File -Recurse | Where-Object { $_.FullName -ne $manifestPath } |
        ForEach-Object { $_.FullName.Substring($package.Length+1).Replace('\','/') })
    $expected = @($manifest.files.PSObject.Properties.Name)
    if (@(Compare-Object $actual $expected).Count -ne 0) { throw 'Manifest file inventory mismatch' }
    foreach ($item in $manifest.files.PSObject.Properties) {
        $relative=$item.Name
        if ($relative -match '(^/|(^|/)\.\.(/|$)|:|\\)') { throw "Unsafe package path: $relative" }
        $file=Join-Path $package $relative.Replace('/','\')
        if (-not (Test-Path -LiteralPath $file -PathType Leaf)) { throw "Missing package file: $relative" }
        $digest=(Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($digest -ne $item.Value.sha256.ToLowerInvariant()) { throw "Tampered file: $relative" }
    }
}
$bin=Join-Path $package 'bin'
$qt=Join-Path $bin 'qt_tools'
Record-Check 'qt_editor' {
    $scene=Join-Path $env:TEMP ('northstar-clean-'+[Guid]::NewGuid().ToString('N')+'.nscn')
    try {
        Launch-Checked (Join-Path $qt 'northstarcpp_editor_qt.exe') @('--smoke','--smoke-output',$scene)
        if (-not (Test-Path -LiteralPath $scene -PathType Leaf)) { throw 'Editor did not serialize scene' }
        Launch-Checked (Join-Path $bin 'northstarcpp_player.exe') @('--warp','--smoke','--scene',$scene)
    } finally {
        Remove-Item -LiteralPath $scene -ErrorAction SilentlyContinue
    }
}
Record-Check 'qt_tools' {
    foreach ($cmd in @(
        @('northstarcpp_project_manager.exe','--smoke'),
        @('northstarcpp_asset_browser.exe','--smoke'),
        @('northstarcpp_camera_timeline_editor.exe','--self-test')
    )) {
        Launch-Checked (Join-Path $qt $cmd[0]) @($cmd[1])
    }

}
Record-Check 'source_project_gameplay' {
    if (-not (Test-Path -LiteralPath (Join-Path $source 'main.nscene') -PathType Leaf)) {
        throw 'Clean-install source project missing'
    }
    if (-not (Test-Path -LiteralPath (Join-Path $bin 'scripting_v8.dll') -PathType Leaf)) {
        throw 'V8 provider missing'
    }
    $app=Join-Path $env:TEMP ('northstar-source-'+[Guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $app -Force | Out-Null
    try {
        $env:LOCALAPPDATA=$app
        $env:NORTHSTARCPP_SCRIPTING_PLUGIN = (Join-Path $bin 'scripting_v8.dll')
        Launch-Checked (Join-Path $bin 'northstarcpp_player.exe') @(
            '--warp','--fps','--scene',(Join-Path $source 'main.nscene'))
        Check-Markers $app
    } finally {
        Remove-Item Env:NORTHSTARCPP_SCRIPTING_PLUGIN -ErrorAction SilentlyContinue
        Remove-Item -LiteralPath $app -Recurse -Force -ErrorAction SilentlyContinue
    }
}
Record-Check 'archive_project_gameplay' {
    if (-not (Test-Path -LiteralPath $archive -PathType Leaf)) {
        throw 'Clean-install archive fixture missing'
    }
    $app=Join-Path $env:TEMP ('northstar-archive-'+[Guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $app -Force | Out-Null
    try {
        $env:LOCALAPPDATA=$app
        $env:NORTHSTARCPP_SCRIPTING_PLUGIN = (Join-Path $bin 'scripting_v8.dll')
        Launch-Checked (Join-Path $bin 'northstarcpp_player.exe') @(
            '--warp','--fps','--archive',$archive,'--scene','level/main.nscene')
        Check-Markers $app
    } finally {
        Remove-Item Env:NORTHSTARCPP_SCRIPTING_PLUGIN -ErrorAction SilentlyContinue
        Remove-Item -LiteralPath $app -Recurse -Force -ErrorAction SilentlyContinue
    }
}

$passed=$true
foreach ($v in $checks.Values) { if (-not $v) { $passed=$false } }
$manifestHash = if (Test-Path -LiteralPath $manifestPath) {
    (Get-FileHash -LiteralPath $manifestPath -Algorithm SHA256).Hash.ToLowerInvariant()
} else { $null }
$record=[ordered]@{
    schema = 1
    classification = 'unsigned_clean_windows_qa_evidence'
    release_approved = $false
    tested_utc = (Get-Date).ToUniversalTime().ToString('o')
    machine_name = $env:COMPUTERNAME
    windows_build = [Environment]::OSVersion.Version.ToString()
    package_manifest_sha256 = $manifestHash
    tested_with_developer_path = $false
    checks = $checks
    failures = $logs
    qa_passed = $passed
    requires_independent_release_signature = $true
}
$dest=Split-Path -Parent $reportPath
if (-not (Test-Path -LiteralPath $dest)) { New-Item -ItemType Directory -Path $dest -Force | Out-Null }
$record | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $reportPath -Encoding UTF8
Write-Output ('Clean Windows QA: '+ $(if ($passed) {'PASS'} else {'FAILED'}) + ' -> '+ $reportPath)
if ($passed) { exit 0 } else { exit 2 }
