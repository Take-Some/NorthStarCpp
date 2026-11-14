@echo off
setlocal
if not "%~1"=="" (
    echo Usage: build_release.cmd
    exit /b 2
)
pushd "%~dp0.." || exit /b 1
python -B -m tools.build_dist --build-type Release --output-dir "%CD%\distributable"
set "EXITCODE=%ERRORLEVEL%"
popd
exit /b %EXITCODE%
