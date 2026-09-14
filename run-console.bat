@echo off
rem Launch the Godot operator console. Any arguments are passed through to Godot,
rem e.g.  run-console.bat --headless --quit-after 60
rem Set ROVER_URL beforehand to point at tools\fake_rover.py instead of the real rover.
rem cd firmware
rem python -m platformio run -t upload -t monitor
rem On a successful join it prints:
rem Rover online at ws://<ip>:81/ (protocol v2, fw ...)
rem $env:ROVER_URL = "ws://192.168.1.42:81/"  then run-console.bat

setlocal

if not defined GODOT_BIN set "GODOT_BIN=C:\Users\User\Programs\Godot_v4.6.33\Godot_v4.6.3-stable_win64_console.exe"

if not exist "%GODOT_BIN%" (
    echo Godot not found at: %GODOT_BIN%
    echo Set GODOT_BIN to the Godot_v4.6.3-stable_win64_console.exe path and retry.
    exit /b 1
)

"%GODOT_BIN%" --path "%~dp0console" %*
exit /b %ERRORLEVEL%
