@echo off
setlocal EnableExtensions
pushd "%~dp0..\.." || exit /b 2

rem Canonical CMake/Ninja build. No project-manager-qt/gcc-debug/gcc-release trees.
set "BUILD_DIR=%CD%\out\build"
set "APP=%BUILD_DIR%\northstarcpp_project_manager.exe"
set "PATH=C:\msys64\mingw64\bin;%PATH%"
set "CMAKE=cmake"
where cmake >nul 2>nul
if errorlevel 1 if exist "C:\Program Files\CMake\bin\cmake.exe" set "CMAKE=C:\Program Files\CMake\bin\cmake.exe"

if /I "%~1"=="--check" goto check
if /I "%~1"=="--no-build" goto launch
if not exist "C:\msys64\mingw64\bin\g++.exe" goto missing
if not exist "C:\msys64\mingw64\bin\ninja.exe" goto missing
if not exist "C:\msys64\mingw64\lib\cmake\Qt6\Qt6Config.cmake" goto missing

rem Reconfigure into the SAME tree even when switching Debug/Release.
"%CMAKE%" -S "%CD%" -B "%BUILD_DIR%" -G Ninja ^
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_SUPPRESS_REGENERATION=OFF ^
  "-DCMAKE_C_COMPILER=C:\msys64\mingw64\bin\gcc.exe" ^
  "-DCMAKE_CXX_COMPILER=C:\msys64\mingw64\bin\g++.exe" ^
  "-DCMAKE_MAKE_PROGRAM=C:\msys64\mingw64\bin\ninja.exe"
if errorlevel 1 goto fail
"%CMAKE%" --build "%BUILD_DIR%" --target northstarcpp_project_manager --parallel 4
if errorlevel 1 goto fail

:launch
if not exist "%APP%" goto missing
start "" "%APP%"
if errorlevel 1 goto fail
echo [NorthStarCpp] Project Manager: %APP%
popd
exit /b 0

:check
if not exist "%APP%" goto missing
echo [NorthStarCpp] Canonical Project Manager: %APP%
popd
exit /b 0

:missing
echo [NorthStarCpp] CMake, MinGW, Qt or canonical Player build is missing.
echo [NorthStarCpp] Build only into: %BUILD_DIR%
:fail
popd
exit /b 1
