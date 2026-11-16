@echo off
setlocal EnableExtensions
for %%I in ("%~dp0..\out\NorthStarCpp-0.5.0-vehicle-native-fix-QA\bin") do set "NORTHSTAR_BIN=%%~fI"
if not exist "%NORTHSTAR_BIN%\vehicle_native.dll" (
    echo ERROR: engine.vehicle provider missing at "%NORTHSTAR_BIN%\vehicle_native.dll".
    pause
    exit /b 2
)
if not exist "%NORTHSTAR_BIN%\northstarcpp_project_manager.exe" (
    echo ERROR: NorthStarCpp 0.5.0 Project Manager not found.
    pause
    exit /b 3
)
pushd "%NORTHSTAR_BIN%"
start "" "%NORTHSTAR_BIN%\northstarcpp_project_manager.exe"
set "EXIT_CODE=%ERRORLEVEL%"
popd
exit /b %EXIT_CODE%
