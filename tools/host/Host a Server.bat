@echo off
rem Double-click to host a Gobby Kart race on this PC: opens the game as the
rem host. It starts the race server, asks your router to open the port, and
rem shows (and copies) the address friends type into ONLINE > JOIN A GAME.
rem If Windows asks whether gk_server may use the network, click Allow.
cd /d "%~dp0"
start "" gk_client.exe --host
