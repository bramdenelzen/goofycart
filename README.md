# Gobby Kart

A multiplayer kart racer in the style of Mario Kart 8 Deluxe, written in C++20
with Vulkan. Low-res, low-poly art; 12 racers per race with CPU drivers filling
empty seats; a dedicated server with server-authoritative racing over UDP.

Status: early development. See [docs/ROADMAP.md](docs/ROADMAP.md) for phases.
Right now you pick Grand Prix, VS race or Time Trials, a cup or course, an
engine class and CPU difficulty, then a driver, body, tires and glider (with
MK8-style stat bars), and race 3 laps on one of 8 themed tracks against 11
CPU drivers: drifting with blue, orange and purple mini-turbos,
hops, tricks off ramps, a glider section over a chasm, dash panels, rocket
starts, off-road, sand, ice and mud, respawns, and item boxes with all 22
race items. The same race
runs online on a dedicated server with up to 12 players.

## Layout

| Path | What it is |
| --- | --- |
| `src/core` | Logging, maths, fixed 60 Hz timestep |
| `src/sim` | `gk_sim`: the pure simulation (karts, track, items). No rendering or OS calls. Shared by client, server and bots |
| `src/net` | `gk_net`: wire protocol, server race room, client prediction, ENet transport |
| `src/server` | `gk_server`: headless dedicated server; `gk_botclient`: headless test players |
| `src/client` | `gk_client`: SDL3 window and input, Vulkan renderer |
| `shaders` | GLSL sources and prebuilt SPIR-V headers |
| `data` | Level tables (`physics.json`) and parts (`parts.json`); every MK8-derived number is tagged datamined or provisional |
| `tests` | Unit tests (doctest) |

## Getting the game without building

Every push to the default branch replaces the **latest** pre-release on the
GitHub Releases page with a ready-to-run Windows zip and macOS DMG: download,
unzip or open, play. Tagged versions (`v0.2.0`) get their own release.

## Building

You need CMake 3.24+, Ninja and a C++20 compiler (MSVC 2022, Clang 16+ or
GCC 13+). Every library is fetched at a pinned version during configure, so no
package manager or Vulkan SDK is needed to build.

```sh
cmake --preset release
cmake --build --preset release
ctest --preset release
```

Only the first configure on a machine downloads the libraries (into `.deps/`,
shared by every build folder). Install [ccache](https://ccache.dev)
(`brew install ccache`, `choco install ccache`, `apt install ccache`) and it is
used automatically: after the first build, even a build from an empty folder
takes seconds.

The `server` preset builds only the dedicated server and tests, with no SDL or
Vulkan, for headless Linux machines.

Linux also needs the usual SDL development packages (X11, Wayland, ALSA,
PulseAudio, udev); `.github/workflows/ci.yml` lists them for Ubuntu.

If `glslangValidator` is on the PATH (it ships with the Vulkan SDK), shaders are
compiled during the build; otherwise the checked-in headers in
`shaders/prebuilt` are used. After editing a shader without it, regenerate
those with `tools/compile_shaders.py`.

## Running

Release packages (from the Release workflow, or `cpack --config
build/release/CPackConfig.cmake`) are one portable folder: unzip and run
`gk_client`. On macOS the DMG holds `Gobby Kart.app` with MoltenVK inside.

```sh
./build/release/src/client/gk_client
./build/release/src/server/gk_server
```

`gk_client` on its own opens the title menu: Grand Prix runs the four
tracks of a cup with points and standings, VS race is a single track, and
Time Trials is a solo run with three mushrooms and no item boxes. To race
online, start a server and point clients at it:

```sh
./build/release/src/server/gk_server --port 7777        # UDP
./build/release/src/client/gk_client --connect 127.0.0.1:7777 --name Albert
./build/release/src/server/gk_botclient --clients 3     # optional: 3 headless players
```

A room runs lobby, race and results in a loop, over a Grand Prix of 4 races
with MK8 points (15, 12, 10, 9 ... 1). In the lobby everyone picks a kart and
presses ready; the race starts 2 seconds after everyone is ready, or when the
20-second timer runs out. There are always 12 karts: players take over bot
karts from the back of the grid, anyone who joins after the countdown
spectates until the next race, and a bot takes over a kart whose player
leaves. Karts that cross the line drive on by themselves; the race ends when
everyone has finished, 4 seconds after the last player finishes, or 30
seconds after the first player finishes. The room waits in the lobby while
nobody is connected (`--bots` keeps it racing).

For public play there is also `gk_master`, an HTTPS service for accounts,
matchmaking and ratings: game servers register with it and ONLINE on the
title menu signs in and finds a race through it. See
[docs/ONLINE.md](docs/ONLINE.md), and [docs/DEPLOY.md](docs/DEPLOY.md) for
running the master and region servers with Docker or systemd.

Server flags: `--port`, `--laps`, `--races` (per Grand Prix), `--lobby`
(seconds), `--cc`, `--cpu easy|normal|hard`, `--bots`, `--cup N` (start
on cup N, moving to the next cup after each Grand Prix), `--track id` (always
race one track).

Running the client needs a Vulkan 1.3 driver: your GPU driver on Windows and
Linux. On macOS the packaged app carries MoltenVK; a source build finds
Homebrew's (`brew install molten-vk`, optionally `vulkan-loader`) or the
LunarG Vulkan SDK, and `SDL_VULKAN_LIBRARY=/path/to/lib` picks another.

| Flag | Effect |
| --- | --- |
| `--fullres` | Render at full window resolution instead of the low-res upscaled look (F2 toggles) |
| `--validation` | Enable Vulkan validation layers (or `GK_VALIDATION=1`) |
| `--frames N --screenshot out.png` | Run N frames, save the last one, quit |
| `--autodrive` | Your kart drives itself (skips kart select; online it readies up by itself) |
| `--cc 50\|100\|150\|200` | Engine class (default 150) |
| `--build D,B,T,G` | Driver, body, tires and glider indices from `data/parts.json` |
| `--karts N` | Racers on the grid, 1 to 12 |
| `--cpu easy\|normal\|hard` | CPU driver difficulty offline (default normal) |
| `--track id` | Skip the menus and race that track (ids below) |
| `--mute` | No sound |
| `--time-trial` | With `--track`: a Time Trial instead of a VS race |
| `--menu options\|course` | Open on that menu (for screenshots) |
| `--connect host[:port]` | Race online on a `gk_server` (default port 7777) |
| `--name NAME` | Your name online (and your account name the first time) |
| `--master URL --region R` | Master server and preferred region for ONLINE |
| `--lag MS --loss PCT --jitter MS` | Simulate a bad connection (added round trip, packet loss, jitter) |

## Racers and parts

16 drivers in seven weight classes (Pip and Tiko are the lightest, Brute and
Ozzy the heaviest), 10 bodies (karts, a buggy, a tub, a trike, a hauler and
three bikes), 8 tyre sets and 5 gliders, all in `data/parts.json`. Each part
adds stat points; the sum per stat is the kart's level 0 to 20. Each part
also has a `look` (colours, a hat or body shape, tyre size) that the client
builds the low-poly kart from, so new parts need no art files.

## Tracks

Tracks are data: each file in `data/tracks/` is a closed spline of
`[x, y, z, half_width]` control points plus features placed by
`[control point, metres]`: trick ramps, glide ramps over a gap, boost pads
(by lane), road surfaces (offroad, sand, ice, water), verges, item box rows
and stretches without walls. `data/tracks/tracks.json` groups them into cups.
The game builds the collision mesh, respawn points, CPU racing line and a
themed low-poly render mesh with scenery from that.

| Cup | Tracks (ids) |
| --- | --- |
| Acorn Cup | `meadow_circuit`, `dune_drift`, `harbor_hop`, `frost_pass` |
| Comet Cup | `canyon_run`, `lava_works`, `cloud_spire`, `neon_raceway` |

`greybox` is still there as a test track.

## Options

OPTIONS on the title menu sets master, music and effects volume, the low-res
look, fullscreen, control hints, rumble and whether crash reports are sent
(see [docs/ONLINE.md](docs/ONLINE.md#crash-reports)). Settings are saved to
`settings.txt` in the same user data folder as the ghosts.

## Time Trials

A Time Trial is a solo run with three mushrooms against a translucent ghost.
Your best run per track and engine class is saved as a ghost (just the
inputs, about 16 KB) in your user data folder (`%APPDATA%\Gobby\GobbyKart`,
`~/Library/Application Support/Gobby/GobbyKart`, `~/.local/share/Gobby/GobbyKart`).
Until you set a time, the ghost is a hard CPU. A saved ghost is replayed
once before the race and dropped if the physics have changed since.

## Sound

All sound is synthesised when the game starts; there are no sample files.
`src/audio` holds the effects (engine with drift squeal and spark shimmer,
boosts, items, hits, countdown, laps), and a chiptune writer that composes a
looping song for each track from its id and theme: chord progression, bass,
lead melody, arpeggio and drums. The music speeds up on the final lap.
`gk_audiodemo out_dir` renders every effect and one song per mood to WAV
files for listening outside the game.

## Controls

Kart select and lobby: Up/Down pick a part, Left/Right change it, Enter (or
A) to race or ready up, Esc to quit.

Racing: arrows or WASD steer, X or Space accelerate, Z brake, Shift
hop/drift (press it at a ramp lip to trick), Up/Down pitch the glider, E or
Ctrl use item (hold to drag it behind you), C look back, R restart, F1 help, F2 low-res toggle, Esc back to kart
select (online: quit). After the finish, Enter or A continues.

Gamepads (Xbox, PlayStation, Switch Pro and generic) work out of the box:
left stick steer and pitch, A/Cross accelerate, B/Circle brake, R hop/drift,
L or left trigger item, X/Square look back, Back/Select restart.

Rocket start: press accelerate as the "2" fades (about 1 to 1.7 s before GO).
Holding it from the start burns out the engine.

## Items

Item boxes give one of the 22 MK8 Deluxe race items, under our own names:
Banana, Triple Bananas, Green Shell, Triple Green Shells, Red Shell, Triple
Red Shells, Spiked Shell (seeks the leader), Bomb, Mushroom, Triple
Mushrooms, Golden Mushroom, Bullet (autopilot rocket), Ink, Lightning, Star,
Fireball, Boomerang, Chomper (a plant that bites what is ahead), Horn
(shockwave; the only defence against the Spiked Shell), Lucky Eight, Coin and
Ghost (turn invisible and steal an item). You can hold two items. Odds depend
on your place.

Hold the item button to keep a banana, shell or bomb behind you as a shield
and let go to throw it; hold Up to throw a banana or bomb forward, Down to
throw a shell backwards. Triple items circle the kart and block one hit each.
Item timings and odds are provisional (tagged in `src/sim/items.cpp`).

## CPU drivers

CPU karts fill every race to 12 (`src/sim/ai.cpp`). They follow a racing
line computed for each track (wide into corners, inside at the apex, over
boost pads), steer with pure pursuit, hop into drifts on corners tight
enough to need one and hold them for mini-turbos, trick off ramps, time
rocket starts, head for item boxes, steer round karts and bananas ahead,
and use items with intent: shells at the kart in front, red shells when not
leading, bananas and shells held behind as shields, mushrooms on straights,
the horn against incoming shells. Easy CPUs keep to the middle of the road,
never drift and lift off the throttle now and then; hard ones drive the full
line and lean into drifts for faster sparks. Behind the leading player, CPUs
speed up a little (up to 9% at hard), and they ease off when ahead of every
player.

The CPU driver is a pure function of the world state, so online clients
predict CPU karts exactly as the server drives them.

## Netcode

The server is the authority: it runs the only real simulation at 60 Hz, and
clients send inputs, never positions. Over one ENet (UDP) connection, inputs
and snapshots use an unreliable sequenced channel and race setup and events a
reliable one.

- Every input packet repeats the last 8 inputs, so lost packets cost nothing.
- The server sends 30 snapshots a second with every kart's quantised state
  (about 60 bytes each) and its latest input.
- The client predicts: it runs its own copy of the race a few ticks ahead of
  the server, applies your input at once, and when a snapshot arrives rewinds
  to it and replays your stored inputs. Bots are predicted with the same
  driver the server uses; other players are extrapolated with their last
  input. Corrections fade out over about a third of a second.
- The client clock speeds up or slows down slightly so that inputs reach the
  server about 2 ticks before they are needed.

Tests race 4 clients at 150 ms round trip with 2% loss over a simulated link
(`tests/test_net.cpp`), and CI does the same over real UDP with
`gk_botclient`. ENet does not encrypt traffic; that is a known gap before
public servers.

## Legal

Gobby Kart borrows game mechanics, not Nintendo characters, names, art or
code. No decompiled code or ripped data is used.
