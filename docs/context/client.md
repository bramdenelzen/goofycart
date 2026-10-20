# Client app (`gk_client`)

`src/client`: SDL3 window, input, menus, HUD, camera, particles and audio
around the renderer. `main.cpp` parses flags into `AppOptions` (`app.h`;
the README flag table lists the user-facing ones, `main.cpp` has all of
them, including screenshot/testing flags like `--pixel`, `--no-ui`,
`--give-item`, `--demo-fx`, `--spawn-obstacle`, `--seam-tour`,
`--export-assets`, `--preview-model`).

## `app.cpp` map (~2900 lines; grep for the function)

| Area | `App::` functions |
| --- | --- |
| Start-up, loop | `run`, `init`, `update`, `render`, `shutdown` |
| Screens | `Screen` enum in `app.h` (Title, Options, Controls, Online, OnlineMenu, Join, Hosting, Course, Select, Race, Standings); `handle_menu_actions`, `render_menu` |
| Races offline | `show_track`, `start_race`, `finish_offline_race`, `draw_standings`, Time Trial: `ghost_path`, `finish_time_trial`, `draw_ghost` |
| Online | `start_matchmaking`, `poll_matchmaking`, `connect_online`, `start_join`, `start_hosting` / `poll_hosting` / `stop_hosting` (spawns a local `gk_server`), `leave_online`, `draw_online_results`, `draw_net_status` |
| Karts | `build_visual`, `set_kart_meshes`, `draw_kart`, `draw_kart_visual`, `draw_glider_visual`; `draw_kart` poses wheelies, two wheels and hits from sim state (`hit_angle`/`hit_speed`: spin direction and turns, flip axis, height under a tunnel roof, surface and water) |
| Effects | `handle_events`, `event_particles`, `emit_kart_particles`, `burst`, `explosion_burst`, `draw_flame`, `draw_speed_lines` |
| World drawing | `draw_items`, `draw_held_item`, `draw_movers`, `draw_gates`, `draw_breakables`, `update_lava`, `draw_water_overlay` |
| Camera | `update_camera`, `spring`, `seam_tour_step`; on a loop it rolls fully with the road and follows the kart's own forward (`cam_dir3_`) |
| Sound | `event_sound`, `update_audio`, `play_at`; engines as voices: `update_kart_engines` (every kart's loop, distance, pan, Doppler from the camera's motion), `engine_for_seat`, `adopt_engine`, `send_engine`; OPTIONS > RECORD ENGINE: `start_engine_recording`, `update_engine_recording` |
| Pause | `pause_items`, `update_pause`, `draw_pause` |

## Other files

- `frontend.*`: title, online, join, hosting and course screens.
- `menu.*`: kart select (part pickers, stat bars) and the online lobby.
- `hud.*`: lap/section, time, place, speed, coins, mini-turbo, countdown,
  minimap, results tables. `ui_style.h`: the shared 90s arcade look.
- `input.*`: keyboard plus every gamepad merged into one `KartInput`, and
  client-only actions. `settings.*`: `settings.txt` key=value in the user
  data folder (volumes, low-res look, fullscreen, rebinding, crash reports).
- `particles.*`: presentation-only particles. `assets.*`: optional model
  packs (`docs/model-packs.md`).
- `audio_device.*`: feeds `gk_audio`'s mixer to SDL. `voice_recorder.*`:
  the microphone through SDL, for RECORD ENGINE. On macOS the app bundle
  needs `Info.plist.in`'s microphone line and `gk_client.entitlements`
  (passed by `tools/package_macos.sh`), or the hardened runtime stops it.

## Audio (`src/audio`, `gk_audio`)

Everything synthesised at start-up, no sample files. `mixer.h` (`Mixer`,
`Sfx`, `Mood`, `mood_for_theme`), `sfx.cpp` (effects), `music.cpp`
(chiptune writer: a song per track from its id and theme, faster on the
final lap), `synth.h`. `gk_audiodemo out_dir` renders everything to WAV.
Tested without a device in `tests/test_audio.cpp`.

Engines as voices: `voice.h` (`EngineClip`; `make_loop` loops whatever was
said, first sound to last, `make_bot_clip` sings each seat its own, mu-law
`encode_clip`/`decode_clip` for the wire, WAV load/save, `doppler_factor`).
`Mixer::set_kart_engines` plays up to 12 looped clips at `engine_revs`
(the clip's own pitch flat out, lower when slow) times Doppler, gliding so 60 Hz updates never click;
`EngineState::voiced` mutes the synth's own note when the player's recording
plays it. `kLocalEngineGain` keeps the two equally loud. Tested in
`tests/test_voice.cpp`.
