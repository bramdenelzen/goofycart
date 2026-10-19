# Networking and online

Server-authoritative: the server runs the only real `World` at 60 Hz;
clients send inputs, never positions (one exception: `World::adopt_motion`
lets the server take a player's reported motion when plausible).

## `gk_net` (`src/net`)

- `protocol.{h,cpp}`: `kProtocolVersion` (bump when the race changes, since
  both sides build the track from data), `kDefaultPort` 7777, message
  types and structs (`Hello`, `Welcome`, `RaceSetup`, `LobbyMsg`,
  `InputMsg`, `SnapshotMsg`, `EventsMsg`, `ResultsMsg`), `Writer`/`Reader`,
  `encode`/`decode`. Kart state quantised to ~55 bytes, smallest-three
  quaternions. Inputs repeat the last 8 (`kInputRedundancy`); snapshots
  every 2 ticks (30 Hz).
- `room.{h,cpp}`: `Room`, one 12-seat race room: lobby -> race -> results
  loop, Grand Prix of N races with MK8 points, bots fill empty seats and
  take over leavers, late joiners spectate, cup rotation. Driven through a
  `MessageSink`, so tests run it without sockets.
- `client_game.{h,cpp}`: `ClientGame`, prediction: runs a few ticks ahead,
  rewinds to each snapshot (`WorldState`) and replays stored inputs; CPU
  karts predicted with the same driver, other players extrapolated;
  corrections fade over ~1/3 s; clock nudged so inputs arrive ~2 ticks early.
- `transport.{h,cpp}`: ENet `NetServer` / `NetClient`, reliable and
  unreliable sequenced channels, `LinkConditions` for simulated lag, loss
  and jitter. No encryption yet.

## Programs

- `src/server/main.cpp` `gk_server`: flags `--port --laps --races --lobby
  --cc --cpu --bots --cup --track --ticks`, `--master URL` with
  `--server-key --region --name` (`master_link.cpp`), UPnP port mapping
  (`upnp.cpp`).
- `src/server/bot_client.cpp` `gk_botclient`: headless players through the
  real prediction code (`--clients --seconds --lag --loss --jitter`).
- `src/online` `gk_online` / `gk_master`: HTTPS master for accounts,
  server list, matchmaking, ratings and crash reports. `MasterState`
  (`master_state.cpp`, persisted to `accounts.json`), routes listed at the
  top of `master_http.h`, `MasterClient` used by client and server.

## Tests and docs

`tests/test_net.cpp` races 4 clients at 150 ms / 2% loss over a simulated
link; `tests/test_master.cpp` drives the master state and HTTP. CI also
races `gk_botclient` over real UDP. `docs/ONLINE.md` (master API, crash
reports, testing on one machine) and `docs/DEPLOY.md` (Docker compose in
`deploy/master`, `deploy/region`; systemd units in `deploy/systemd`).
