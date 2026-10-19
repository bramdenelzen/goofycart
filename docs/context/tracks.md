# Tracks

Tracks are data. 13 race tracks plus the built-in `greybox` test track.
`docs/TRACKS.md` is the full file-format reference (sections: The main
loop, Point to point, Features, Gates, Branches); read only the section you
need.

## Pipeline

1. `data/tracks/<id>.json` -> `parse_track_def` / `load_track_def`
   (`src/sim/track_builder.cpp`, bottom half) -> `TrackDef`
   (`include/gk/sim/track_def.h`: `TrackPos` = `[control point, metres]`,
   `TrackFeature`, `GateDef`, `BranchDef`).
2. `build_track(def)` (top of `track_builder.cpp`, ~670 lines): Catmull-Rom
   spline resampled every ~2 m, four-lane road, verges, walls, ramps, boost
   pads, glide gaps, item box rows, obstacles, stairs, zones, tunnels,
   movers, switches, barriers; side roads (branches) along their own open
   splines, joined by a shared stretch and a walled nose.
3. `Track::finalize()` (`src/sim/track.cpp`) builds Jolt `MeshShape`
   collision (one per triangle group: 0 fixed track, k branch k-1) and the
   racing line. `Track` (`track.h`) holds samples, checkpoints, gates,
   zones, deep water, currents, vents, tunnels, and query helpers
   (`raycast`, `collide_sphere`, `nearest_sample`, `grid_slot`,
   `respawn_sample_before`, `section_at`).
4. `data/tracks/tracks.json` groups tracks into cups (four per cup, a track
   in one cup only). `TrackCatalog::load`, `make_track(catalog, id)`.

Lap tracks are closed loops; point-to-point tracks (`point_to_point`,
`start`, `finish`, `sections`) are raced once and counted in sections.
Gates (laps, cycles, switches) make things change mid-race; branches are
alternate paths that can open and close.

## Tools and checks

- `gk_trackmap data/tracks/tracks.json out/ [id] [--race|--balance]`
  (`tools/trackmap/trackmap.cpp`, built with the client): top-down PNG with
  control points numbered, self-crossings listed; `--race` runs 12 CPUs.
  Quickest way to try a layout change.
- Bridges need 8 m headroom where the spline crosses itself (tested in
  `tests/test_tracks.cpp`). Other track tests: `test_point_to_point`,
  `test_paths` (branches), `test_props`, `test_tunnels`, `test_water`.
- Visual seams: `gk_client --track <id> --seam-tour <dir>` photographs the
  track; `src/client/render/seam_check.cpp` finds cracks, z-fighting,
  terrain through the road and floating props. Findings and fixes are in
  `docs/SEAMS.md` (578 lines: grep for the track name).

## Rendering a track

`theme` picks palette, scenery, sky and music (meadow, desert, harbor,
snow, canyon, volcano, sky, night, market, beach). Road and scenery meshes
are built from the `Track` in `src/client/render/track_mesh.cpp`
(`make_track_mesh`, `make_scenery_mesh`, `theme_style`); walls, kerbs and
flags in `trackside.cpp`; props in `gobby_props.cpp`. See `rendering.md`.

Adding or changing a track changes the race: bump `kProtocolVersion`, and
add the id to `tracks.json` and the README track table.
