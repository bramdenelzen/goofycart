#!/usr/bin/env bash
# Lays out the macOS release and puts it in a DMG:
#
#   GobbyKart-<version>-macos-<arch>/
#     Gobby Kart.app    (game data in Resources, MoltenVK in Frameworks)
#     gk_server  gk_master  gk_botclient  data/  docs/  README.md
#
# Signs with MACOS_SIGN_IDENTITY (imported from MACOS_CERT_P12) and notarizes
# with APPLE_ID / APPLE_TEAM_ID / APPLE_APP_PASSWORD when those are set;
# otherwise everything is ad-hoc signed.
#
# usage: tools/package_macos.sh <build dir> <output dir>
set -euo pipefail
build=$1
out=$2
version=$(sed -n 's/^CMAKE_PROJECT_VERSION:STATIC=//p' "$build/CMakeCache.txt")
arch=$(uname -m); [ "$arch" = x86_64 ] && arch=x64
name="GobbyKart-$version-macos-$arch"
stage=$(mktemp -d)/$name
mkdir -p "$out"

cmake --install "$build" --component game --prefix "$stage"
mv "$stage/gk_client.app" "$stage/Gobby Kart.app"
app="$stage/Gobby Kart.app"

identity="-"
if [ -n "${MACOS_CERT_P12:-}" ] && [ -n "${MACOS_SIGN_IDENTITY:-}" ]; then
  keychain=$RUNNER_TEMP/signing.keychain-db
  security create-keychain -p ci "$keychain"
  security set-keychain-settings -lut 3600 "$keychain"
  security unlock-keychain -p ci "$keychain"
  echo "$MACOS_CERT_P12" | base64 --decode > "$RUNNER_TEMP/cert.p12"
  security import "$RUNNER_TEMP/cert.p12" -k "$keychain" -P "$MACOS_CERT_PASSWORD" -T /usr/bin/codesign
  security set-key-partition-list -S apple-tool:,apple: -s -k ci "$keychain"
  security list-keychains -d user -s "$keychain" $(security list-keychains -d user | tr -d '"')
  identity=$MACOS_SIGN_IDENTITY
fi
sign() { codesign --force --timestamp --options runtime --sign "$identity" "$@"; }
[ "$identity" = "-" ] && sign() { codesign --force --sign - "$@"; }

for lib in "$app"/Contents/Frameworks/*.dylib; do if [ -f "$lib" ]; then sign "$lib"; fi; done
# The game hosts races by starting gk_server, so the app carries a copy.
cp "$stage/gk_server" "$app/Contents/MacOS/gk_server"
sign "$app/Contents/MacOS/gk_server"
# The game may record from the microphone (its engine sound), which the
# hardened runtime only allows with this entitlement.
sign --entitlements "$(cd "$(dirname "$0")/.." && pwd)/src/client/gk_client.entitlements" "$app"
for tool in gk_server gk_master gk_botclient; do sign "$stage/$tool"; done

dmg="$out/$name.dmg"
hdiutil create -volname "Gobby Kart" -srcfolder "$stage" -ov -format UDZO "$dmg"

if [ "$identity" != "-" ] && [ -n "${APPLE_ID:-}" ]; then
  sign "$dmg"
  xcrun notarytool submit "$dmg" --apple-id "$APPLE_ID" --team-id "$APPLE_TEAM_ID" \
    --password "$APPLE_APP_PASSWORD" --wait
  xcrun stapler staple "$dmg"
  echo "Signed and notarized $dmg"
else
  echo "Ad-hoc signed $dmg (no Developer ID secrets)"
fi
