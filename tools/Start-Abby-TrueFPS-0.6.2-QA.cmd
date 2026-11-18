@echo off
setlocal EnableExtensions
for %%I in ("%~dp0..") do set "NORTHSTAR_ROOT=%%~fI"
set "NORTHSTAR_BIN=%NORTHSTAR_ROOT%\out\build-fps-body-062"
if not exist "%NORTHSTAR_BIN%\northstarcpp_player.exe" (
    echo [NorthStarCpp] QA Player is missing: "%NORTHSTAR_BIN%"
    pause
    exit /b 2
)
if not exist "%NORTHSTAR_BIN%\AssetManager.dll" (
    echo [NorthStarCpp] QA asset provider is missing.
    pause
    exit /b 3
)
if not exist "%NORTHSTAR_ROOT%\Projects\AbbyPistolDemo\main.nscn" (
    echo [NorthStarCpp] Abby Pistol Demo scene not found.
    pause
    exit /b 4
)
pushd "%NORTHSTAR_BIN%"
start "" "%NORTHSTAR_BIN%\northstarcpp_player.exe" --explore --scene "%NORTHSTAR_ROOT%\Projects\AbbyPistolDemo\main.nscn" --shared-assets "%NORTHSTAR_ROOT%\sharedAssets"
set "RESULT=%ERRORLEVEL%"
popd
exit /b %RESULT%
