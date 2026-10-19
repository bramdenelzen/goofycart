# Build, CI and release

## CMake

- Root `CMakeLists.txt`: C++20, options `GK_BUILD_CLIENT`,
  `GK_BUILD_SERVER`, `GK_BUILD_TESTS`, `GK_MACOS_BUNDLE`,
  `GK_COMPILER_CACHE` (ccache/sccache auto-detected). `gk_warnings` carries
  our warning flags (MSVC `/W4`, else `-Wall -Wextra`).
- Targets: `gk_core`, `gk_sim`, `gk_net`, `gk_audio` (+ `gk_audiodemo`),
  `gk_online` (+ `gk_master`), `gk_server`, `gk_botclient`, `gk_client`,
  `gk_trackmap` (client builds only), `gk_tests`.
- `cmake/Dependencies.cmake`: every library fetched at a pinned tag into
  `.deps/` by `gk_fetch` (glm, nlohmann_json, Jolt, ENet, cpp-httplib,
  doctest; client only: SDL3, Vulkan-Headers, volk, vk-bootstrap, VMA,
  stb). No package manager or Vulkan SDK needed.
- `CMakePresets.json`: `debug`, `release`, `server` (no client), each with
  build and test presets, Ninja, output in `build/<preset>`.
- `cmake/Packaging.cmake`: CPack ZIP (Windows), DragNDrop (macOS), TGZ
  (Linux). The release DMG (`Gobby Kart.app` with MoltenVK inside) is laid
  out by `tools/package_macos.sh`, called from `release.yml`.
  `tools/host/*.bat|.command` are the Host/Join/Dedicated Server scripts
  shipped next to the game.

## Workflows (`.github/workflows`)

- `ci.yml`: pull requests (and manual) only. Windows (MSVC 2022) and macOS
  14; configure, build, `ctest`, a server smoke test and a 4-bot online
  smoke test at 150 ms / 2% loss. Caches `.deps` and ccache.
- `release.yml`: pushes to the default branch
  `claude/gobbykart-build-osdxg2` replace the `latest` pre-release with
  Windows zip and macOS DMG; `v*` tags publish a versioned release. macOS
  signing and notarisation run when the Apple secrets are set.

## Gotchas

- A cloud container has no GPU: build with `--preset server` to run tests;
  the client and `gk_trackmap` need the full build.
- Shaders: see `rendering.md` (prebuilt headers must be regenerated).
- `.gitignore` drops `*.png` except under `docs/`, plus `assets/` (model
  packs must never be committed).
