# Gobby Kart

A multiplayer kart racer in the style of Mario Kart 8 Deluxe, written in C++20
with Vulkan. Low-res, low-poly art; 12 racers per race with CPU drivers filling
empty seats; a dedicated server with server-authoritative racing over UDP.

Status: early development. See [docs/ROADMAP.md](docs/ROADMAP.md) for phases.

## Layout

| Path | What it is |
| --- | --- |
| `src/core` | Logging, maths, fixed 60 Hz timestep |
| `src/sim` | `gk_sim`: the pure simulation (karts, track, items). No rendering or OS calls. Shared by client, server and bots |
| `src/server` | `gk_server`: headless dedicated server |
| `src/client` | `gk_client`: SDL3 window and input, Vulkan renderer |
| `shaders` | GLSL sources and prebuilt SPIR-V headers |
| `data` | Stat tables and tuning data |
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

## Controls

Keyboard: arrows or WASD steer, X or Space accelerate, Z or Down brake,
Shift hop/drift, E use item, C look back, Esc quit. Gamepads (Xbox,
PlayStation, Switch Pro and generic) work out of the box: left stick steer,
A/Cross accelerate, B/Circle brake, R hop/drift, L or left trigger item.

## Legal

Gobby Kart borrows game mechanics, not Nintendo characters, names, art or
code. No decompiled code or ripped data is used.
