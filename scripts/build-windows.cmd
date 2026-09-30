@echo off
rem ============================================================================================
rem  MCP DAW - Windows build (MSVC 2022 / Build Tools), then run the headless tests.
rem
rem  Usage:   scripts\build-windows.cmd [Release|Debug]      (or double-click it)
rem  Output:  <build dir>\MCPDAW_artefacts\<config>\MCP-DAW.exe  (+ mcp-daw-bridge.exe beside it)
rem
rem  Needs:   Visual Studio 2022 or Build Tools 2022 with "Desktop development with C++"
rem           (includes CMake and Ninja), and Git (JUCE is downloaded with it on the first build).
rem  JUCE is taken from %JUCE_PATH% if set, else downloaded by CMake (pinned to JUCE 8.0.15).
rem  The build directory is %BUILD_DIR% if set, else .\build\<config> inside the repo.
rem ============================================================================================
setlocal EnableDelayedExpansion

rem Double-clicked from Explorer? Then keep the window open at the end so the result can be read.
set "INTERACTIVE="
echo %cmdcmdline% | findstr /i /c:"%~nx0" >nul && set "INTERACTIVE=1"

set "ROOT=%~dp0.."
pushd "%ROOT%" || ( echo [build] Cannot open the repository folder "%ROOT%" & goto fail_nopop )
set "ROOT=%CD%"

set "CONFIG=Release"
:args
if "%~1"=="" goto args_done
if /i "%~1"=="Debug"   set "CONFIG=Debug"
if /i "%~1"=="Release" set "CONFIG=Release"
shift
goto args
:args_done

if not exist "%ROOT%\CMakeLists.txt" (
    echo [build] !ROOT! does not look like the MCP DAW source folder ^(no CMakeLists.txt^).
    goto fail
)

rem --- Locate Visual Studio / Build Tools and enter the x64 developer environment ---------------
if not defined VCINSTALLDIR (
    set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
    if not exist "!VSWHERE!" (
        echo [build] Visual Studio is not installed.
        echo         Install "Build Tools for Visual Studio 2022" with the "Desktop development with C++"
        echo         workload: https://visualstudio.microsoft.com/downloads/  ^(or: winget install
        echo         Microsoft.VisualStudio.2022.BuildTools^), then run this again.
        goto fail
    )
    for /f "usebackq delims=" %%i in (`"!VSWHERE!" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSPATH=%%i"
    if not defined VSPATH (
        echo [build] Visual Studio is installed but without the C++ tools. Open "Visual Studio Installer",
        echo         click Modify and tick "Desktop development with C++", then run this again.
        goto fail
    )
    echo [build] Visual Studio: !VSPATH!
    call "!VSPATH!\VC\Auxiliary\Build\vcvars64.bat" >nul || ( echo [build] vcvars64.bat failed & goto fail )
)
where cmake >nul 2>&1 || (
    echo [build] CMake not found. It comes with the "Desktop development with C++" workload
    echo         ^(component "C++ CMake tools for Windows"^) - add it in Visual Studio Installer.
    goto fail
)

rem --- JUCE location ----------------------------------------------------------------------------
set "JUCE_ARG="
if defined JUCE_PATH (
    set "JUCE_ARG=-DJUCE_PATH=%JUCE_PATH%"
) else (
    where git >nul 2>&1 || (
        echo [build] Git not found. The first build downloads JUCE with Git: install Git for Windows
        echo         ^(https://git-scm.com/download/win or: winget install Git.Git^), open a NEW
        echo         command prompt, then run this again.
        goto fail
    )
)

rem --- Build directory and generator -------------------------------------------------------------
if not defined BUILD_DIR (
    set "BUILD_DIR=%ROOT%\build\%CONFIG%"
)
set "GENERATOR=NMake Makefiles"
where ninja >nul 2>&1 && set "GENERATOR=Ninja"
echo [build] config=%CONFIG%  generator=%GENERATOR%
echo [build] juce=%JUCE_PATH%
echo [build] dir=%BUILD_DIR%
echo [build] The first build downloads and compiles JUCE: allow 10-20 minutes.

cmake -S "%ROOT%" -B "%BUILD_DIR%" -G "%GENERATOR%" -DCMAKE_BUILD_TYPE=%CONFIG% %JUCE_ARG% || goto fail
cmake --build "%BUILD_DIR%" --parallel || goto fail

echo [build] running tests...
pushd "%BUILD_DIR%"
ctest --output-on-failure || ( popd & goto fail )
popd

echo.
echo [build] OK: %BUILD_DIR%\MCPDAW_artefacts\%CONFIG%\MCP-DAW.exe
popd
if defined INTERACTIVE pause
exit /b 0

:fail
popd
:fail_nopop
echo [build] FAILED
if defined INTERACTIVE pause
exit /b 1
