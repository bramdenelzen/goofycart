# Gobby Kart roadmap

Phases follow the roadmap in the technical research spec
("Gobby Kart: Technical Research"). Each phase ends with a push to GitHub.
Feel first, network second, but the sim / client / server split exists from
day one because it is the one decision that is expensive to retrofit.

| Phase | Scope | Done when |
| --- | --- | --- |
| 1. Foundation (done) | CMake presets, pinned dependencies, CI on Windows, macOS and Linux; SDL3 window; Vulkan 1.3 renderer with low-res upscaling; headless `gk_server`; unit tests | CI green on all three; client renders a low-poly scene |
| 2. Feel prototype (done) | Greybox track with hills, a ramp, a glide section and every surface class; kart controller with MK8-style stats, drift, mini-turbos (blue, orange, purple), tricks, rocket start, gliders, off-road, respawn; chase camera; HUD; gamepad | One kart drives a full lap on a controller |
| 3. Netcode spine (done) | UDP transport with reliable and unreliable lanes; server at 60 Hz; inputs, snapshots, prediction and reconciliation, remote kart extrapolation; latency and loss simulation | 2 to 4 clients race smoothly at 150 ms and 2% loss |
| 4. Race loop (done) | Laps and ordered checkpoints, places, results with MK8 points, countdown, lobby, 12 seats with bot fill and disconnect takeover | A 12-kart race runs start to finish on the server |
| 5. Items (done) | Item boxes and server-side roulette, two-slot holding, distribution by place and gap; core items first, then all 22 race items | Every item works online |
| 6. CPU drivers (done) | Racing lines, pure-pursuit steering, drifting, item use, difficulty and rubber-banding | Bots finish races competitively |
| 7a. Tracks and menus (done) | Data-driven tracks (spline + features), 8 themed tracks in 2 cups, title / cup / course menus, offline Grand Prix with standings, VS race, Time Trials, server cup rotation | Every track finishes a 12-CPU race; menus reach every mode |
| 7b. Content and backend (done) | Audio, Time Trial ghosts, options menu, 16 drivers and 23 parts, master server with accounts, matchmaking and ratings over HTTPS | Bots sign up, matchmake and race through the master in CI |
| 8. Ship | Portable packages (zip, tarball, DMG with MoltenVK) built by a Release workflow, macOS signing and notarisation when the secrets are set, crash reports sent to the master, Docker and systemd deployment for the master and region servers, soak playtest | `v*` tag publishes packages for all three platforms; a containerised master + race server + bots race in CI |

## Physics parity

Constants that come from Mario Kart 8 Deluxe are tagged in the data files as
`datamined` (from the Super Mario Wiki tables or MK8 Deluxe Builder) or
`provisional` (a placeholder until it is measured from 60 fps captures).
Mario Kart Wii decompilation code is never copied; only plain-language notes
on mechanics are used (clean-room rule from the spec).
