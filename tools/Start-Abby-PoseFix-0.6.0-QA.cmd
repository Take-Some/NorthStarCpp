@echo off
setlocal EnableExtensions
for %%I in ("%~dp0..") do set "NORTHSTAR_ROOT=%%~fI"
set "NORTHSTAR_BIN=%NORTHSTAR_ROOT%\out\NorthStarCpp-0.6.0-AbbyPistol-PoseFix-QA\bin"
if not exist "%NORTHSTAR_BIN%\vehicle_native.dll" (
    echo NorthStarCpp QA: required vehicle_native.dll is missing.
    pause
    exit /b 2
)
if not exist "%NORTHSTAR_BIN%\northstarcpp_project_manager.exe" (
    echo NorthStarCpp QA: Project Manager is missing.
    pause
    exit /b 3
)
pushd "%NORTHSTAR_BIN%"
start "" "%NORTHSTAR_BIN%\northstarcpp_project_manager.exe"
set "RESULT=%ERRORLEVEL%"
popd
exit /b %RESULT%
