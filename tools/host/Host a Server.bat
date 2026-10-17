@echo off
rem Double-click to host a Gobby Kart race on this PC. Friends then use
rem "Join a Server.bat" with the address shown below. Close this window to stop.
setlocal
cd /d "%~dp0"
title Gobby Kart server
set PORT=7777

rem Let other computers reach the server (asks for permission once).
netsh advfirewall firewall show rule name=GobbyKartServer >nul 2>&1
if errorlevel 1 (
  echo Asking Windows to allow the server through the firewall...
  powershell -NoProfile -Command "Start-Process netsh -Verb RunAs -Wait -ArgumentList 'advfirewall','firewall','add','rule','name=GobbyKartServer','dir=in','action=allow','protocol=UDP','localport=%PORT%'" >nul 2>&1
)

set LANIP=
for /f "usebackq delims=" %%i in (`powershell -NoProfile -Command "(Get-NetIPConfiguration | Where-Object { $_.IPv4DefaultGateway -ne $null -and $_.NetAdapter.Status -eq 'Up' } | Select-Object -First 1).IPv4Address.IPAddress"`) do set LANIP=%%i
set PUBIP=
for /f "usebackq delims=" %%i in (`curl -s --max-time 5 https://api.ipify.org`) do set PUBIP=%%i

echo.
echo  ==============================================================
echo   Gobby Kart server is starting on port %PORT%
echo.
echo   Friends on your Wi-Fi join with:   %LANIP%
if defined PUBIP echo   Friends elsewhere join with:       %PUBIP%
if defined PUBIP echo     (only after your router forwards UDP port %PORT% to %LANIP%)
echo   You can play too: run "Join a Server.bat" and type 127.0.0.1
echo.
echo   Close this window to stop the server.
echo  ==============================================================
echo.
gk_server.exe --port %PORT%
echo.
echo The server stopped.
pause
