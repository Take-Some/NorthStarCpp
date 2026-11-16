@echo off
setlocal
for %%I in ("%~dp0..\..") do set "ROOT=%%~fI"
if defined NORTHSTARCPP_PLAYER_EXE (
  set "PLAYER=%NORTHSTARCPP_PLAYER_EXE%"
) else (
  set "PLAYER=%ROOT%\..\cache\qt-pie-icons-build\northstarcpp_player.exe"
)
if not exist "%PLAYER%" (
  echo [ERROR] Current NorthStarCpp Player executable was not found.
  echo Expected: "%PLAYER%"
  echo Build the current NorthStarCpp player or set NORTHSTARCPP_PLAYER_EXE.
  exit /b 2
)
rem Physics extension for Possess/CharacterMotor is implemented by Jolt.
set "NORTHSTARCPP_PHYSICS_PROVIDER=jolt"
set "NORTHSTARCPP_RENDER_PROVIDER=d3d12"
cd /d "%ROOT%"
"%PLAYER%" --explore --scene "%~dp0main.nscn" --shared-assets "%ROOT%\sharedAssets" %*
exit /b %ERRORLEVEL%
