#!/bin/bash
# Double-click to host a Gobby Kart race on this Mac. Friends then use
# "Join a Server.command" with the address shown below. Close the window to stop.
cd "$(dirname "$0")" || exit 1
PORT=7777
# Downloaded programs are quarantined until opened once; this script was.
xattr -dr com.apple.quarantine . 2>/dev/null
LANIP=$(ipconfig getifaddr en0 2>/dev/null || ipconfig getifaddr en1 2>/dev/null)
PUBIP=$(curl -s --max-time 5 https://api.ipify.org)
cat <<MSG

 ==============================================================
  Gobby Kart server is starting on port $PORT

  Friends on your Wi-Fi join with:   ${LANIP:-"(not on a network)"}
MSG
if [ -n "$PUBIP" ]; then
  echo "  Friends elsewhere join with:       $PUBIP"
  echo "    (only after your router forwards UDP port $PORT to ${LANIP:-this Mac})"
fi
cat <<MSG
  You can play too: open "Join a Server.command" and type 127.0.0.1
  If macOS asks whether gk_server may accept connections, click Allow.

  Close this window to stop the server.
 ==============================================================

MSG
# A writable place for the server's crash reports (the DMG is read-only).
mkdir -p "$HOME/Library/Application Support/Gobby/GobbyKart/server"
SERVER="$PWD/gk_server"
cd "$HOME/Library/Application Support/Gobby/GobbyKart/server" && "$SERVER" --port "$PORT"
echo "The server stopped."
