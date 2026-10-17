#!/bin/bash
# Double-click to join a Gobby Kart race. Remembers the last address and name.
cd "$(dirname "$0")" || exit 1
xattr -dr com.apple.quarantine . 2>/dev/null
DIR="$HOME/Library/Application Support/Gobby/GobbyKart"
SAVE="$DIR/join.txt"
ADDR=""; NAME=""
if [ -f "$SAVE" ]; then
  ADDR=$(sed -n 's/^address=//p' "$SAVE"); NAME=$(sed -n 's/^name=//p' "$SAVE")
fi
read -r -p "Server address${ADDR:+ [$ADDR]} (the host's window shows it): " NEW
ADDR=${NEW:-${ADDR:-127.0.0.1}}
read -r -p "Your name${NAME:+ [$NAME]}: " NEW
NAME=${NEW:-${NAME:-Player}}
mkdir -p "$DIR" && printf 'address=%s\nname=%s\n' "$ADDR" "$NAME" > "$SAVE"
open "Gobby Kart.app" --args --connect "$ADDR" --name "$NAME"
