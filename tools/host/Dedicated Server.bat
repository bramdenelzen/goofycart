@echo off
rem Double-click to run only the race server (no game) on this PC, for a
rem computer that stays on. Close this window to stop it.
cd /d "%~dp0"
title Gobby Kart server
gk_server.exe --port 7777 --upnp --workdir "%LOCALAPPDATA%\Gobby\GobbyKart\server"
echo.
echo The server stopped.
pause
