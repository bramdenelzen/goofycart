@echo off
rem Double-click to join a Gobby Kart race. Remembers the last address and name.
setlocal
cd /d "%~dp0"
set SAVE=%APPDATA%\Gobby\GobbyKart\join.txt
set ADDR=
set NAME=
if exist "%SAVE%" (
  for /f "usebackq tokens=1,* delims==" %%a in ("%SAVE%") do (
    if "%%a"=="address" set ADDR=%%b
    if "%%a"=="name" set NAME=%%b
  )
)
if defined ADDR (
  set /p NEWADDR=Server address [%ADDR%]: 
) else (
  set /p NEWADDR=Server address ^(the host's screen shows it, e.g. 192.168.1.20^): 
)
if defined NEWADDR set ADDR=%NEWADDR%
if defined NAME (
  set /p NEWNAME=Your name [%NAME%]: 
) else (
  set /p NEWNAME=Your name: 
)
if defined NEWNAME set NAME=%NEWNAME%
if not defined ADDR set ADDR=127.0.0.1
if not defined NAME set NAME=Player
if not exist "%APPDATA%\Gobby\GobbyKart" mkdir "%APPDATA%\Gobby\GobbyKart"
(echo address=%ADDR%& echo name=%NAME%) > "%SAVE%"
start "" gk_client.exe --connect %ADDR% --name "%NAME%"
