#!/bin/bash
# Double-click to host a Gobby Kart race on this Mac: opens the game as the
# host. It starts the race server, asks your router to open the port, and
# shows (and copies) the address friends type into ONLINE > JOIN A GAME.
cd "$(dirname "$0")" || exit 1
# Downloaded programs are quarantined until opened once; this script was.
xattr -dr com.apple.quarantine . 2>/dev/null
open "Gobby Kart.app" --args --host
