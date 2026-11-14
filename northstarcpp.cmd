@echo off
setlocal EnableExtensions
if "%~1"=="" goto browser
if /I "%~1"=="project-manager" goto browser
rem Legacy development commands remain available while CLI migration continues.
set "PYTHONPATH=%~dp0;%PYTHONPATH%"
python -B -m tools.dev.cli %*
exit /b %ERRORLEVEL%

:browser
call "%~dp0tools\dev\start.cmd" %2
exit /b %ERRORLEVEL%
