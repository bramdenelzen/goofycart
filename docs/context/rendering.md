# Rendering

## Renderer

`src/client/render/renderer.{h,cpp}`: Vulkan 1.3 forward renderer (volk,
vk-bootstrap, VMA) with dynamic rendering. Draws to a low-resolution
offscreen colour + depth target, then blits with nearest filtering (the
low-res look; `--fullres`/F2 turns it off). Pipelines: shadow
(`shadow.vert`), mesh (`mesh.vert/frag`: lighting, point lights, texture
array, sky sampling, lava, water), post (`post.frag`: sun shafts, mist,
heat shimmer) and UI (`ui.vert/frag`: SDF text, rects, icons). Particles
and decals are batched sprites (`Renderer::Sprite`). On macOS it runs
through MoltenVK.

Shaders live in `shaders/`. If `glslangValidator` is on PATH they compile
during the build; otherwise the checked-in `shaders/prebuilt/*.h` are used.
After editing a shader, regenerate those with `tools/compile_shaders.py`
and commit them.

## Meshes and textures (all procedural)

- `mesh.{h,cpp}`: `MeshBuilder`, 40-byte vertex, flat and smooth
  primitives (box, cone, sphere, lathe, loft, tube...).
- `textures.cpp`: 128x128 grey detail layers in one texture array that
  multiply vertex colour. `sky.cpp`: painted equirectangular sky per theme.
- `track_mesh.cpp` (~1900 lines): `theme_style` (palette per theme),
  `make_track_mesh` (road, walls, underside), `make_scenery_mesh` (ground,
  hills, tunnels, grandstands, market town, islands), zone, barrier,
  switch, vent, current and lava meshes. `lava.h` mirrors the lava shader's
  bubbles on the CPU and must match `lava_color()` in `mesh.frag`.
- `trackside.cpp`: painted walls, rails, kerbs, flags.
- `kart_mesh.cpp`: low-poly karts and gliders from each part's `look`.
- Painted models (smooth meshes with baked hand-painted textures, via
  `paint.cpp` and `gobby_util.h`; exportable with `--export-assets`):
  `gobby_kart.cpp` (standard kart), `gobby_meowie.cpp` (Meowie driver),
  `gobby_items.cpp`, `gobby_items2.cpp` (items), `gobby_props.cpp` (map
  props). Declared in `gobby_assets.h`.
- `item_mesh.cpp`, `item_icons.cpp`: placeholder item models, HUD icon atlas.
- `font.cpp`: SDF font baked from `data/fonts/RussoOne-Regular.ttf`.
- `model.cpp`: OBJ and COLLADA loader for model packs.
- `seam_check.cpp`: geometry checks behind `--seam-tour` and `docs/SEAMS.md`.

## Seeing a change

`gk_client --track <id> --frames 300 --screenshot out.png` (add
`--autodrive`, `--no-ui`, `--pixel`, `--spawn-sample N`, `--lap N`). Needs
a Vulkan driver, so not in a headless container.
