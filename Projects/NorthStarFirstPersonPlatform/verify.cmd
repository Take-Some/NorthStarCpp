@echo off
setlocal
for %%I in ("%~dp0..\..") do set "ROOT=%%~fI"
set "PLAYER=%ROOT%\out\fps-release-qa-20261114\northstarcpp_player.exe"
if not exist "%PLAYER%" (
  echo [ERROR] Current NorthStarCpp development Player is not built.
  echo Expected: "%PLAYER%"
  echo Build: cmake --build out/fps-release-qa-20261114 --target northstarcpp_player
  exit /b 2
)
cd /d "%ROOT%"
"%PLAYER%" --warp --check-scene --explore --scene "%~dp0main.nscn" --shared-assets "%ROOT%\sharedAssets" %*
if errorlevel 1 exit /b %ERRORLEVEL%
"%PLAYER%" --warp --runtime-init-smoke --explore --scene "%~dp0main.nscn" --shared-assets "%ROOT%\sharedAssets"
exit /b %ERRORLEVEL%
