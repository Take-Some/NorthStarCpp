@echo off
setlocal
for %%I in ("%~dp0..\..") do set "ROOT=%%~fI"
if defined NORTHSTARCPP_PLAYER_EXE (
  set "PLAYER=%NORTHSTARCPP_PLAYER_EXE%"
) else (
  set "PLAYER=%ROOT%\..\cache\qt-pie-icons-build\northstarcpp_player.exe"
)
if not exist "%PLAYER%" (
  echo [ERROR] Current Player not built: "%PLAYER%"
  exit /b 2
)
set "NORTHSTARCPP_PHYSICS_PROVIDER=jolt"
set "NORTHSTARCPP_RENDER_PROVIDER=d3d12"
cd /d "%ROOT%"
"%PLAYER%" --warp --check-scene --explore --scene "%~dp0main.nscn" --shared-assets "%ROOT%\sharedAssets"
if errorlevel 1 exit /b %ERRORLEVEL%
"%PLAYER%" --warp --runtime-init-smoke --explore --scene "%~dp0main.nscn" --shared-assets "%ROOT%\sharedAssets"
exit /b %ERRORLEVEL%
