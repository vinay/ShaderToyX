@echo off
setlocal EnableDelayedExpansion

rem ---------------------------------------------------------------
rem  ShaderToyX build script
rem
rem  Invokes cl.exe directly; no MSBuild or Visual Studio project
rem  involvement. The .vcxproj is only for people building inside
rem  the Visual Studio IDE.
rem
rem  PREREQUISITE
rem    This script expects the MSVC x64 toolchain to be on PATH.
rem    Run it from an "x64 Native Tools Command Prompt for VS", or
rem    call vcvarsall.bat with the x64 flag first, e.g.:
rem
rem      "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat" x64
rem
rem  USAGE
rem    build.bat            Build Debug and Release
rem    build.bat debug      Build Debug only
rem    build.bat release    Build Release only
rem    build.bat all        Build Debug and Release
rem    build.bat clean      Delete the build\ directory
rem
rem  OUTPUT
rem    build\debug\ShaderToyX.exe   (+ .pdb, obj\ intermediates)
rem    build\release\ShaderToyX.exe (+ .pdb, obj\ intermediates)
rem ---------------------------------------------------------------

set "ROOT=%~dp0"
set "SRC=%ROOT%ShaderToyX\src"
set "MANIFEST=%ROOT%ShaderToyX\ShaderToyX.manifest"
set "BUILD_DIR=%ROOT%build"

set "MODE=%~1"
if "%MODE%"=="" set "MODE=all"

if /i "%MODE%"=="clean" (
    if exist "%BUILD_DIR%" (
        echo Removing "%BUILD_DIR%"...
        rmdir /s /q "%BUILD_DIR%"
    )
    echo Clean complete.
    exit /b 0
)

if /i not "%MODE%"=="debug" if /i not "%MODE%"=="release" if /i not "%MODE%"=="all" (
    echo Unknown option: %MODE%
    echo Usage: build.bat [debug^|release^|all^|clean]
    exit /b 1
)

rem ---- Check the toolchain environment ----
where cl.exe >nul 2>nul
if errorlevel 1 (
    echo ERROR: cl.exe is not on PATH.
    echo        Run this from an "x64 Native Tools Command Prompt for VS",
    echo        or run vcvarsall.bat x64 first. See the comment at the top of this script.
    exit /b 1
)

rem vcvarsall.bat sets VSCMD_ARG_TGT_ARCH to the target it was run for.
if defined VSCMD_ARG_TGT_ARCH (
    if /i not "%VSCMD_ARG_TGT_ARCH%"=="x64" (
        echo ERROR: the toolchain on PATH targets %VSCMD_ARG_TGT_ARCH%, but this script builds x64.
        echo        Run vcvarsall.bat with the x64 flag.
        exit /b 1
    )
) else (
    echo WARNING: VSCMD_ARG_TGT_ARCH is not set; assuming cl.exe on PATH targets x64.
)

rem ---- Flags shared by both configurations ----
rem   /W3 /sdl /permissive- /std:c++20 /EHsc  mirror the .vcxproj settings
rem   /MP                                     compile the source files in parallel
set "CFLAGS_COMMON=/nologo /W3 /sdl /permissive- /std:c++20 /EHsc /MP /Zi"
set "CFLAGS_COMMON=%CFLAGS_COMMON% /D_CRT_SECURE_NO_WARNINGS /DWIN32_LEAN_AND_MEAN /DNOMINMAX /DUNICODE /D_UNICODE"

set "LFLAGS_COMMON=/NOLOGO /SUBSYSTEM:WINDOWS /DEBUG /MANIFEST:EMBED /MANIFESTINPUT:"%MANIFEST%""
set "LIBS=kernel32.lib user32.lib gdi32.lib opengl32.lib comctl32.lib"

set "FAILED=0"

if /i "%MODE%"=="debug"   call :build debug
if /i "%MODE%"=="release" call :build release
if /i "%MODE%"=="all" (
    call :build debug
    call :build release
)

if "%FAILED%"=="1" (
    echo.
    echo BUILD FAILED.
    exit /b 1
)

echo.
echo Build complete.
exit /b 0

rem ---------------------------------------------------------------
rem  :build <debug|release>
rem ---------------------------------------------------------------
:build
set "CFG=%~1"
set "OUT=%BUILD_DIR%\%CFG%\"
set "OBJ=%OUT%obj\"

if /i "%CFG%"=="debug" (
    rem /MDd defines _DEBUG; /RTC1 enables run-time checks
    set "CFLAGS=%CFLAGS_COMMON% /MDd /Od /RTC1"
    set "LFLAGS=%LFLAGS_COMMON%"
) else (
    set "CFLAGS=%CFLAGS_COMMON% /MD /O2 /Oi /GL /Gy /DNDEBUG"
    set "LFLAGS=%LFLAGS_COMMON% /LTCG /OPT:REF /OPT:ICF"
)

echo.
echo ===== Building %CFG% ^(x64^) -^> %OUT%
if not exist "%OBJ%" mkdir "%OBJ%"

rem Note the doubled trailing backslash in /Fo: a single "\" before the
rem closing quote would be read as an escaped quote.
cl !CFLAGS! ^
    /Fo"%OBJ%\" ^
    /Fd"%OBJ%ShaderToyX.pdb" ^
    /Fe"%OUT%ShaderToyX.exe" ^
    "%SRC%\*.cpp" ^
    /link !LFLAGS! %LIBS% /PDB:"%OUT%ShaderToyX.pdb"

if errorlevel 1 set "FAILED=1"
exit /b 0
