@echo off
setlocal EnableExtensions
pushd "%~dp0..\.." || (
    echo [NorthStarCpp] Engine repository is not accessible.
    exit /b 2
)

rem Qt 6 Widgets is the ONLY supported Project Browser UI.
rem Prefer a verified standalone Release package, never an old Win32/GDI build.
set "APP="
for /f "delims=" %%P in ('dir /b /ad /o-n "dist\NorthStarCpp-*-windows-x64-Release" 2^>nul') do (
    if not defined APP if exist "dist\%%P\qt.runtime.json" if exist "dist\%%P\bin\platforms\qwindows.dll" if exist "dist\%%P\bin\northstarcpp_project_manager.exe" set "APP=%CD%\dist\%%P\bin\northstarcpp_project_manager.exe"
)
if defined APP goto ready
set "BUILD_DIR=%CD%\out\project-manager-qt"
if exist "%BUILD_DIR%\Qt6Widgets.dll" if exist "%BUILD_DIR%\northstarcpp_project_manager.exe" set "APP=%BUILD_DIR%\northstarcpp_project_manager.exe"
if /I "%~1"=="--check" goto skip_build
if /I "%~1"=="--no-build" goto skip_build
goto build

:skip_build
if not defined APP if exist "out\qt-browser-debug\Qt6Widgets.dll" if exist "out\qt-browser-debug\northstarcpp_project_manager.exe" set "APP=%CD%\out\qt-browser-debug\northstarcpp_project_manager.exe"

:ready
if /I "%~1"=="--check" goto check
goto launch

:build
rem A dedicated CMake build tree avoids touching a running game build.
set "CMAKE=cmake"
where cmake >nul 2>nul
if errorlevel 1 (
    if not exist "C:\Program Files\CMake\bin\cmake.exe" (
        echo [NorthStarCpp] CMake is required.
        goto fail
    )
    set "CMAKE=C:\Program Files\CMake\bin\cmake.exe"
)
if not exist "C:\msys64\mingw64\bin\g++.exe" (
    echo [NorthStarCpp] Missing GCC MinGW-w64 C++ compiler.
    goto fail
)
if not exist "C:\msys64\mingw64\lib\cmake\Qt6\Qt6Config.cmake" (
    echo [NorthStarCpp] Qt 6 Widgets SDK is missing. Install Qt6 for mingw64.
    goto fail
)
if not exist "C:\msys64\mingw64\bin\ninja.exe" (
    echo [NorthStarCpp] Ninja is required.
    goto fail
)
if not exist "%BUILD_DIR%\CMakeCache.txt" goto configure
if not exist "%BUILD_DIR%\build.ninja" goto configure
findstr /I /R /C:"^CMAKE_SUPPRESS_REGENERATION:.*=ON$" "%BUILD_DIR%\CMakeCache.txt" >nul
if not errorlevel 1 goto configure
goto compile

:configure
"%CMAKE%" -S "%CD%" -B "%BUILD_DIR%" -G Ninja ^
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_SUPPRESS_REGENERATION=OFF ^
  "-DCMAKE_C_COMPILER=C:\msys64\mingw64\bin\gcc.exe" ^
  "-DCMAKE_CXX_COMPILER=C:\msys64\mingw64\bin\g++.exe" ^
  "-DCMAKE_MAKE_PROGRAM=C:\msys64\mingw64\bin\ninja.exe"
if errorlevel 1 goto fail
:compile
echo [NorthStarCpp] Checking the Qt Editor, Player and game runtime ...
"%CMAKE%" --build "%BUILD_DIR%" --target northstarcpp_project_manager --parallel 4
if errorlevel 1 goto fail
if exist "%BUILD_DIR%\Qt6Widgets.dll" if exist "%BUILD_DIR%\northstarcpp_project_manager.exe" set "APP=%BUILD_DIR%\northstarcpp_project_manager.exe"

:launch
if not defined APP (
    echo [NorthStarCpp] Qt Project Browser could not be located or compiled.
    goto fail
)
start "" "%APP%"
if errorlevel 1 goto fail
echo [NorthStarCpp] Started Qt Project Browser: "%APP%"
popd
exit /b 0

:check
if not defined APP (
    echo [NorthStarCpp] No Qt Project Browser build found.
    goto fail
)
echo [NorthStarCpp] Qt Project Browser: "%APP%"
popd
exit /b 0

:fail
echo [NorthStarCpp] Legacy Win32/Python browser will not be launched.
popd
exit /b 1
