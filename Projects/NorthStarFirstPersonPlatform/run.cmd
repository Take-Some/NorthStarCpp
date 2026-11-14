@echo off
setlocal
for %%I in ("%~dp0..\..") do set "ROOT=%%~fI"
set "PLAYER=%ROOT%\out\qa-fps-dist-20261114\NorthStarCpp-0.4.0-windows-x64-Release-QA\bin\northstarcpp_player.exe"
if not exist "%PLAYER%" (
  echo [ERROR] Verified NorthStarCpp QA Player is not installed.
  echo Expected: "%PLAYER%"
  exit /b 2
)
cd /d "%ROOT%"
"%PLAYER%" --explore --scene "%~dp0main.nscn" %*
exit /b %ERRORLEVEL%
