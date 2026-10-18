#!/bin/bash
# Double-click to run only the race server (no game) on this Mac, for a
# computer that stays on. Close the window to stop it.
cd "$(dirname "$0")" || exit 1
xattr -dr com.apple.quarantine . 2>/dev/null
DIR="$HOME/Library/Application Support/Gobby/GobbyKart/server"
mkdir -p "$DIR"
"$PWD/gk_server" --port 7777 --upnp --workdir "$DIR"
echo "The server stopped."
