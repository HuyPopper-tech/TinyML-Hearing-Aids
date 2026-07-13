@echo off
REM ===========================================================================
REM  build.bat - one-command build for M55M1_GestureAI (Windows)
REM
REM  Usage:
REM    build.bat            configure + build (Release)
REM    build.bat clean      remove build/ then configure + build
REM    build.bat config      configure only
REM    build.bat only        build only (skip configure)
REM
REM  Requires Arm Compiler for Embedded 6.24+ on PATH, plus CMake and Ninja.
REM  If armclang is NOT on PATH, this script auto-adds the default install
REM  folder (C:\Program Files\ArmCompilerforEmbedded6.24\bin).
REM ===========================================================================
setlocal enableextensions

set "PROJ_ROOT=%~dp0"
set "ARMCLANG_BIN=C:\Program Files\ArmCompilerforEmbedded6.24\bin"

REM Auto-add Arm Compiler to PATH for this session if not already there
where armclang >nul 2>&1
if errorlevel 1 (
    if exist "%ARMCLANG_BIN%" set "PATH=%ARMCLANG_BIN%;%PATH%"
)

REM Pick a Ninja if not on PATH (STM32CubeCLT ships one)
where ninja >nul 2>&1
if errorlevel 1 (
    if exist "C:\ST\STM32CubeCLT_1.20.0\Ninja\bin\ninja.exe" (
        set "CMAKE_MAKE_PROGRAM=C:/ST/STM32CubeCLT_1.20.0/Ninja/bin/ninja.exe"
    )
)

set "TC=%PROJ_ROOT%cmake\armclang.cmake"

if /i "%~1"=="clean" (
    if exist "%PROJ_ROOT%build" rmdir /s /q "%PROJ_ROOT%build"
)

if /i "%~1"=="only" goto :build

:config
cmake -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE="%TC%" -DCMAKE_BUILD_TYPE=Release -Wno-dev
if errorlevel 1 exit /b 1

REM Mirror build\compile_commands.json to the project root for clangd / editors
REM that only look at the root. (CMake generates it during the configure above.)
if exist "%PROJ_ROOT%build\compile_commands.json" (
    if exist "%PROJ_ROOT%compile_commands.json" (
        attrib -s -h -r "%PROJ_ROOT%compile_commands.json" >nul 2>&1
        del /f /q "%PROJ_ROOT%compile_commands.json" >nul 2>&1
    )
    copy /y "%PROJ_ROOT%build\compile_commands.json" "%PROJ_ROOT%compile_commands.json" >nul
)

if /i "%~1"=="config" exit /b 0

:build
cmake --build build
if errorlevel 1 exit /b 1

echo.
echo Build OK. Output in build\:
dir /b "%PROJ_ROOT%build\M55M1_*"
endlocal
