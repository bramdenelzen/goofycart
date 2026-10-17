# Gobby Kart

A multiplayer kart racer in the style of Mario Kart 8 Deluxe, written in C++20
with Vulkan. Low-res, low-poly art; 12 racers per race with CPU drivers filling
empty seats; a dedicated server with server-authoritative racing over UDP.

Status: early development. See [docs/ROADMAP.md](docs/ROADMAP.md) for phases.
Right now you can race 3 laps of a greybox test track against 11 simple
line-following karts: drifting with blue, orange and purple mini-turbos,
hops, tricks off ramps, a glider section over a chasm, dash panels, rocket
starts, off-road, sand, ice and mud, and Lakitu-style respawns.

## Layout

| Path | What it is |
| --- | --- |
| `src/core` | Logging, maths, fixed 60 Hz timestep |
| `src/sim` | `gk_sim`: the pure simulation (karts, track, items). No rendering or OS calls. Shared by client, server and bots |
| `src/server` | `gk_server`: headless dedicated server |
| `src/client` | `gk_client`: SDL3 window and input, Vulkan renderer |
| `shaders` | GLSL sources and prebuilt SPIR-V headers |
| `data` | Level tables (`physics.json`) and parts (`parts.json`); every MK8-derived number is tagged datamined or provisional |
| `tests` | Unit tests (doctest) |

## Building

You need CMake 3.24+, Ninja and a C++20 compiler (MSVC 2022, Clang 16+ or
GCC 13+). Every library is fetched at a pinned version during configure, so no
package manager or Vulkan SDK is needed to build.

```sh
cmake --preset release
cmake --build --preset release
ctest --preset release
```

The `server` preset builds only the dedicated server and tests, with no SDL or
Vulkan, for headless Linux machines.

Linux also needs the usual SDL development packages (X11, Wayland, ALSA,
PulseAudio, udev); `.github/workflows/ci.yml` lists them for Ubuntu.

If `glslangValidator` is on the PATH (it ships with the Vulkan SDK), shaders are
compiled during the build; otherwise the checked-in headers in
`shaders/prebuilt` are used. After editing a shader without it, regenerate
those with `tools/compile_shaders.py`.

## Running

```sh
./build/release/src/client/gk_client
./build/release/src/server/gk_server
```

Running the client needs a Vulkan 1.3 driver: your GPU driver on Windows and
Linux, or MoltenVK / KosmicKrisp on macOS (install the LunarG Vulkan SDK).

| Flag | Effect |
| --- | --- |
| `--fullres` | Render at full window resolution instead of the low-res upscaled look (F2 toggles) |
| `--validation` | Enable Vulkan validation layers (or `GK_VALIDATION=1`) |
| `--frames N --screenshot out.png` | Run N frames, save the last one, quit |
| `--autodrive` | Your kart drives itself |
| `--cc 50\|100\|150\|200` | Engine class (default 150) |
| `--build D,B,T,G` | Driver, body, tires and glider indices from `data/parts.json` |
| `--karts N` | Racers on the grid, 1 to 12 |

## Controls

Keyboard: arrows or WASD steer, X or Space accelerate, Z brake, Shift
hop/drift (press it at a ramp lip to trick), Up/Down pitch the glider, E use
item, C look back, R restart, F1 help, F2 low-res toggle, Esc quit.

Gamepads (Xbox, PlayStation, Switch Pro and generic) work out of the box:
left stick steer and pitch, A/Cross accelerate, B/Circle brake, R hop/drift,
L or left trigger item, X/Square look back, Back/Select restart.

Rocket start: press accelerate as the "2" fades (about 1 to 1.7 s before GO).
Holding it from the start burns out the engine.

## Legal

Gobby Kart borrows game mechanics, not Nintendo characters, names, art or
code. No decompiled code or ripped data is used.
