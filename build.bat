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
rem    build.bat                  Build Debug and Release
rem    build.bat debug            Build Debug only
rem    build.bat release          Build Release only
rem    build.bat all              Build Debug and Release
rem    build.bat package [ver]    Build Release and zip it up for distribution.
rem                               [ver] defaults to `git describe --tags`.
rem    build.bat clean            Delete the build\ directory
rem
rem  OUTPUT
rem    build\debug\ShaderToyX.exe   (+ .pdb, obj\ intermediates)
rem    build\release\ShaderToyX.exe (+ .pdb, obj\ intermediates)
rem    build\ShaderToyX-<ver>-win64.zip          (package: exe + README + LICENSE)
rem    build\ShaderToyX-<ver>-win64-symbols.zip  (package: matching .pdb)
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

set "VALID="
for %%m in (debug release all package) do if /i "%MODE%"=="%%m" set "VALID=1"
if not defined VALID (
    echo Unknown option: %MODE%
    echo Usage: build.bat [debug^|release^|all^|package [version]^|clean]
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
set "LIBS=kernel32.lib user32.lib gdi32.lib opengl32.lib comctl32.lib ole32.lib"

set "FAILED=0"

if /i "%MODE%"=="debug"   call :build debug
if /i "%MODE%"=="release" call :build release
if /i "%MODE%"=="all" (
    call :build debug
    call :build release
)
if /i "%MODE%"=="package" (
    call :build release
    if "!FAILED!"=="0" call :package "%~2"
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

rem ---------------------------------------------------------------
rem  :package [version]
rem  Zips build\release\ShaderToyX.exe + README + LICENSE into
rem  build\ShaderToyX-<version>-win64.zip, and the .pdb into a
rem  matching -symbols.zip.
rem ---------------------------------------------------------------
:package
set "VERSION=%~1"
if "%VERSION%"=="" (
    for /f "usebackq delims=" %%v in (`git -C "%ROOT%." describe --tags --always --dirty 2^>nul`) do set "VERSION=%%v"
)
if "%VERSION%"=="" set "VERSION=dev"

set "PKG_NAME=ShaderToyX-%VERSION%-win64"
set "STAGE=%BUILD_DIR%\%PKG_NAME%"
set "ZIP=%BUILD_DIR%\%PKG_NAME%.zip"
set "SYMZIP=%BUILD_DIR%\%PKG_NAME%-symbols.zip"

echo.
echo ===== Packaging %PKG_NAME%
if exist "%STAGE%" rmdir /s /q "%STAGE%"
mkdir "%STAGE%"
copy /y "%BUILD_DIR%\release\ShaderToyX.exe" "%STAGE%\" >nul
copy /y "%ROOT%README.md"                     "%STAGE%\" >nul
copy /y "%ROOT%LICENSE"                       "%STAGE%\" >nul

if exist "%ZIP%"    del /q "%ZIP%"
if exist "%SYMZIP%" del /q "%SYMZIP%"

powershell -NoProfile -ExecutionPolicy Bypass -Command ^
    "Compress-Archive -Path '%STAGE%\*' -DestinationPath '%ZIP%' -CompressionLevel Optimal; " ^
    "Compress-Archive -Path '%BUILD_DIR%\release\ShaderToyX.pdb' -DestinationPath '%SYMZIP%' -CompressionLevel Optimal"
if errorlevel 1 (
    echo ERROR: packaging failed.
    set "FAILED=1"
    exit /b 0
)

rmdir /s /q "%STAGE%"
echo Created %ZIP%
echo Created %SYMZIP%
exit /b 0
