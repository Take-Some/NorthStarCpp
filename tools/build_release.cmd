@echo off
setlocal EnableExtensions
if not "%~1"=="" (
    echo Usage: build_release.cmd
    exit /b 2
)
pushd "%~dp0.." || exit /b 1
rem Release QA builds use the same out/build tree as the editor and Player.
rem Publish ONLY after the separate signed clean-machine approval gate.
python -B -m tools.build_dist --build-type Release --qa-only --output-dir "%CD%\out\package\qa"
set "EXITCODE=%ERRORLEVEL%"
popd
exit /b %EXITCODE%
