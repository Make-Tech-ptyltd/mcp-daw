@echo off
rem ============================================================================================
rem  MCP DAW - Windows build (MSVC 2022 / Build Tools), then run the headless tests.
rem
rem  Usage:   scripts\build-windows.cmd [Release|Debug]
rem  Output:  <build dir>\MCPDAW_artefacts\<config>\MCP-DAW.exe  (+ mcp-daw-bridge.exe beside it)
rem
rem  JUCE is taken from %JUCE_PATH% if set, else downloaded by CMake (pinned to JUCE 8.0.15).
rem  The build directory is %BUILD_DIR% if set, else .\build\<config> inside the repo.
rem ============================================================================================
setlocal EnableDelayedExpansion
set "ROOT=%~dp0.."
pushd "%ROOT%" || exit /b 1
set "ROOT=%CD%"

set "CONFIG=Release"
:args
if "%~1"=="" goto args_done
if /i "%~1"=="Debug"   set "CONFIG=Debug"
if /i "%~1"=="Release" set "CONFIG=Release"
shift
goto args
:args_done

rem --- Locate Visual Studio / Build Tools and enter the x64 developer environment ---------------
if not defined VCINSTALLDIR (
    set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
    if not exist "!VSWHERE!" ( echo [build] vswhere.exe not found - install Visual Studio 2022 Build Tools & exit /b 1 )
    for /f "usebackq delims=" %%i in (`"!VSWHERE!" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSPATH=%%i"
    if not defined VSPATH ( echo [build] No MSVC x64 toolset found & exit /b 1 )
    call "!VSPATH!\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
)

rem --- JUCE location ----------------------------------------------------------------------------
set "JUCE_ARG="
if defined JUCE_PATH set "JUCE_ARG=-DJUCE_PATH=%JUCE_PATH%"

rem --- Build directory and generator -------------------------------------------------------------
if not defined BUILD_DIR (
    set "BUILD_DIR=%ROOT%\build\%CONFIG%"
)
set "GENERATOR=NMake Makefiles"
where ninja >nul 2>&1 && set "GENERATOR=Ninja"
echo [build] config=%CONFIG%  generator=%GENERATOR%
echo [build] juce=%JUCE_PATH%
echo [build] dir=%BUILD_DIR%

cmake -S "%ROOT%" -B "%BUILD_DIR%" -G "%GENERATOR%" -DCMAKE_BUILD_TYPE=%CONFIG% %JUCE_ARG% || goto fail
cmake --build "%BUILD_DIR%" --parallel || goto fail

echo [build] running tests...
pushd "%BUILD_DIR%"
ctest --output-on-failure || ( popd & goto fail )
popd

echo.
echo [build] OK: %BUILD_DIR%\MCPDAW_artefacts\%CONFIG%\MCP-DAW.exe
popd
exit /b 0

:fail
echo [build] FAILED
popd
exit /b 1
