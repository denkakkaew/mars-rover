@echo off
rem Launch the operator station: the vision detector in its own window, then the console.
rem
rem   run-all.bat                          camera at %CAMERA_URL%, or the default below
rem   run-all.bat http://192.168.1.39      camera board's base URL (xiaostream)
rem
rem The detector posts to the console's detection API on 127.0.0.1:8765; it does not mind
rem starting first, and the console shows DETECTOR OFFLINE until its snapshots arrive.
rem Set ROVER_URL beforehand to point the console at tools\fake_rover.py, as with
rem run-console.bat. Close the detector with q or Esc in its video window.

setlocal

if not "%~1"=="" set "CAMERA_URL=%~1"
if not defined CAMERA_URL set "CAMERA_URL=http://192.168.1.39"

where python >nul 2>nul
if errorlevel 1 (
    echo Python not found on PATH; the vision detector needs it.
    exit /b 1
)

echo Starting vision detector against %CAMERA_URL% ...
rem cmd /k keeps the window open if the detector exits, so its error stays readable.
start "Vision detector" cmd /k python "%~dp0vision\detect_stream.py" --url %CAMERA_URL%

echo Starting operator console ...
call "%~dp0run-console.bat"
exit /b %ERRORLEVEL%
