@echo off
setlocal
cd /d "%~dp0..\.."
set "PLAYER=out\gcc-shared-assets-release\northstarcpp_player.exe"
if not exist "%PLAYER%" (echo [ERROR] Build NorthStarCpp player first.& exit /b 2)
"%PLAYER%" --explore --scene "Projects\NorthStarFirstPersonPlatform\main.nscn" %*
exit /b %ERRORLEVEL%
