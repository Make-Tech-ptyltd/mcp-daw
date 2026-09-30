@echo off
rem MCP DAW demo: builds three songs live through MCP while they play (about 4 minutes).
rem Needs mcp-daw-bridge.exe next to MCP-DAW.exe one folder up (as in the release zip),
rem or MCP_DAW_BRIDGE set to its full path.
title MCP DAW - Demo
setlocal
cd /d "%~dp0"
set "BRIDGE=%~dp0..\mcp-daw-bridge.exe"
if not exist "%BRIDGE%" if defined MCP_DAW_BRIDGE set "BRIDGE=%MCP_DAW_BRIDGE%"
if not exist "%BRIDGE%" (
  echo Could not find mcp-daw-bridge.exe.
  echo Keep this Demo folder next to MCP-DAW.exe and mcp-daw-bridge.exe, as it came in the zip.
  pause
  exit /b 1
)
if not exist "samples\*.wav" (
  echo The demo samples are missing: this folder needs its "samples" subfolder.
  pause
  exit /b 1
)
echo.
echo   MCP DAW is about to open and play a demo that it builds as it goes.
echo   Turn your speakers up. This window narrates; close it to stop the demo.
echo.
"%BRIDGE%" --script "demo-script.txt"
echo.
pause
