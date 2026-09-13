@echo off
REM ===========================================================================
REM  build.bat - profile-aware build for M55M1 GestureAI (Windows)
REM
REM  Usage: build.bat [release|debug] [build|config|only|clean]
REM    build.bat                 Release: configure + build (legacy default)
REM    build.bat debug           Debug:   configure + build into build-debug\
REM    build.bat debug config    Configure Debug only
REM    build.bat release only    Build an existing Release tree only
REM    build.bat clean           Clean + rebuild Release (legacy command)
REM    build.bat debug clean     Clean + rebuild Debug
REM ===========================================================================
setlocal enableextensions

set "PROJ_ROOT=%~dp0"
if "%PROJ_ROOT:~-1%"=="\" set "PROJ_ROOT=%PROJ_ROOT:~0,-1%"
set "ARMCLANG_BIN=C:\Program Files\ArmCompilerforEmbedded6.24\bin"
set "PROFILE=release"
set "ACTION=build"

REM Parse the optional profile, then the optional action. The legacy one-word
REM actions remain Release commands.
if /i "%~1"=="release" (
    set "PROFILE=release"
    shift
) else if /i "%~1"=="debug" (
    set "PROFILE=debug"
    shift
)

if /i "%~1"=="build" (
    set "ACTION=build"
    shift
) else if /i "%~1"=="config" (
    set "ACTION=config"
    shift
) else if /i "%~1"=="only" (
    set "ACTION=only"
    shift
) else if /i "%~1"=="clean" (
    set "ACTION=clean"
    shift
) else if not "%~1"=="" (
    goto :usage
)

if not "%~1"=="" goto :usage

if /i "%PROFILE%"=="debug" (
    set "BUILD_DIR=build-debug"
    set "BUILD_TYPE=Debug"
) else (
    set "BUILD_DIR=build"
    set "BUILD_TYPE=Release"
)
set "BUILD_PATH=%PROJ_ROOT%\%BUILD_DIR%"
set "TC=%PROJ_ROOT%\cmake\armclang.cmake"

REM Auto-add Arm Compiler to PATH for this session if not already there.
where armclang >nul 2>&1
if errorlevel 1 if exist "%ARMCLANG_BIN%" set "PATH=%ARMCLANG_BIN%;%PATH%"

REM Pick a Ninja if not on PATH (STM32CubeCLT ships one).
where ninja >nul 2>&1
if errorlevel 1 if exist "C:\ST\STM32CubeCLT_1.20.0\Ninja\bin\ninja.exe" set "PATH=C:\ST\STM32CubeCLT_1.20.0\Ninja\bin;%PATH%"

if /i "%ACTION%"=="clean" if exist "%BUILD_PATH%" rmdir /s /q "%BUILD_PATH%"
if /i "%ACTION%"=="only" goto :build

cmake -S "%PROJ_ROOT%" -B "%BUILD_PATH%" -G Ninja -DCMAKE_TOOLCHAIN_FILE="%TC%" -DCMAKE_BUILD_TYPE=%BUILD_TYPE% -DM55M1_MODEL_PACKAGE_DIR= -DM55M1_ENABLE_TFLM=OFF -DM55M1_ENABLE_DAC_I2S_OUTPUT=ON -Wno-dev
if errorlevel 1 goto :failed

REM Point clangd at the most recently configured profile.
if exist "%BUILD_PATH%\compile_commands.json" (
    if exist "%PROJ_ROOT%\compile_commands.json" (
        attrib -s -h -r "%PROJ_ROOT%\compile_commands.json" >nul 2>&1
        del /f /q "%PROJ_ROOT%\compile_commands.json" >nul 2>&1
    )
    copy /y "%BUILD_PATH%\compile_commands.json" "%PROJ_ROOT%\compile_commands.json" >nul
)

if /i "%ACTION%"=="config" goto :success

:build
cmake --build "%BUILD_PATH%"
if errorlevel 1 goto :failed

echo.
echo Build OK. Output in %BUILD_DIR%\:
dir /b "%BUILD_PATH%\M55M1_*"

:success
endlocal & exit /b 0

:failed
endlocal & exit /b 1

:usage
echo Usage: build.bat [release^|debug] [build^|config^|only^|clean]
endlocal & exit /b 2
