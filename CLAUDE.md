# Gobby Kart: agent index

A 12-racer kart game in the style of Mario Kart 8 Deluxe: C++20, Vulkan 1.3
via SDL3, a deterministic shared simulation, and a server-authoritative
dedicated server over UDP (ENet). No image or sample files: meshes, textures,
sky, sound and music are all generated in code.

This file is an index. Read only the area file(s) your task touches, and grep
large files instead of reading them whole (`README.md` is 16 KB,
`docs/SEAMS.md` 578 lines, `src/client/app.cpp` ~2900 lines,
`src/client/render/renderer.cpp` and `track_mesh.cpp` ~1900 each).
Words with a fixed meaning (glider, glide ramp, deep water, point-to-point
track, section, cup) are defined in `CONTEXT.md`.

## Build, test, run

```sh
cmake --preset release            # first run fetches pinned deps into .deps/
cmake --build --preset release
ctest --preset release            # one doctest binary: build/release/tests/gk_tests
./build/release/src/client/gk_client --track meadow_circuit
./build/release/src/server/gk_server --port 7777
```

The `server` preset builds without SDL/Vulkan (headless Linux, cloud
containers). Run one test case with `gk_tests -tc="name*"`.

## Rules that hold everywhere

- `gk_sim` is pure and deterministic: no rendering, OS calls, clocks or
  outside randomness. The server, client prediction, CPU drivers and ghosts
  all rely on it.
- A change that alters how a race plays (physics, items, track data or
  building) means bumping `kProtocolVersion` in
  `src/net/include/gk/net/protocol.h`; saved ghosts self-invalidate.
- MK8-derived numbers in `data/*.json` and `src/sim/items.cpp` are tagged
  `datamined`, `official` or `provisional`; keep the tag accurate. Never copy
  decompiled code or ripped data; never commit model packs (`assets/`).
- The default branch is `claude/gobbykart-build-osdxg2`. CI runs on PRs only
  (Windows + macOS); pushes to the default branch run the Release workflow.

## Areas

| Read | When the task touches |
| --- | --- |
| `docs/context/sim.md` | `World`, the tick, determinism, `KartInput`, tests and the sim fixture |
| `docs/context/karts.md` | Kart physics, drift and boosts, gliding, water, stats, parts, `physics.json`, `parts.json` |
| `docs/context/tracks.md` | Track JSON, the track builder, collision, gates/branches, cups, `gk_trackmap`, seams |
| `docs/context/items.md` | The 22 items, roulette odds, thrown entities, hits |
| `docs/context/ai.md` | CPU drivers, racing line, difficulty, rubber-banding |
| `docs/context/client.md` | `gk_client`: app loop, menus, HUD, input, settings, camera, particles, audio |
| `docs/context/rendering.md` | Vulkan renderer, shaders, track/scenery meshes, painted models, model packs |
| `docs/context/networking.md` | Protocol, server room, client prediction, `gk_server`, `gk_botclient`, master server, deploy |
| `docs/context/build-ci.md` | CMake, dependencies, presets, CI and Release workflows, packaging, shaders build |

## Outside the repo

The project's shared folder holds large reference images (MK8 screenshots,
Albert's item and character paintings, track photos) and rendered audio.
They cost many tokens to view: open one only when the task names it.
