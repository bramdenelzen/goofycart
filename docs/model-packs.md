# Model packs

The client can replace its procedural karts, drivers, tyres, gliders and items
with model files. A pack is a folder `assets/models/` next to `data/` with a
`manifest.json`; the folder is gitignored, because packs made from ripped game
assets (such as the Mario Kart 8 rips from The Models Resource) are
copyrighted and must never be committed or shipped. Without a pack, or for any
part the pack leaves out, the game draws its own meshes.

Formats: COLLADA (`.dae`, as exported by Blender or Switch Toolbox) and
Wavefront OBJ with MTL, with PNG/JPG/TGA/BMP or DDS (BC1/2/3) textures. Only
the diffuse (albedo) texture is used; normal, specular and bake maps are
ignored. Skinned meshes are drawn in their bind pose.

## manifest.json

```json
{
  "drivers": { "<driver id>": { "file": "...", "height": 1.1 } },
  "bodies":  { "<body id>":   { "file": "...", "length": 1.9, "wheels": [[x, z], ...], "seat": [x, y, z] } },
  "tires":   { "<tire id>":   { "file": "...", "radius": 0.26 } },
  "gliders": { "<glider id>": { "file": "...", "width": 2.6 } },
  "items":   { "banana": { "file": "...", "height": 0.7 } }
}
```

Ids are the `id` fields in `data/parts.json`. Item names: `banana`,
`green_shell`, `red_shell`, `spiked_shell`, `bomb`, `item_box`, `bullet`,
`chomper`, `boomerang`, `fireball`.

Each entry is a path (relative to the manifest, `..` allowed) or an object:

| key | meaning |
| --- | --- |
| `file` | one path, or a list of paths merged into one model |
| `yaw`, `pitch`, `roll` | degrees, applied first; the game's forward is +Z, up +Y |
| `length` / `width` / `height` / `radius` | fit that dimension (Z / X / Y / half of max(Y, Z)) to this many metres |
| `scale` | extra multiplier after the fit |
| `anchor` | `bottom` (min Y at 0, XZ centred, the default for most), `center`, or `none` |
| `offset` | metres, added last |
| `texture` | a file that replaces every part's texture (a red shell from the green one) |
| `tint`, `alpha` | multiply the texture colour (0..1 each) and draw translucent |
| `two_sided` | draw without back-face culling |
| `wheels`, `seat`, `wheel_radius` | bodies: where the tyres and driver go, in metres after fitting |

Tyres are drawn at the body's wheel centres and spin with the kart.
`gk_client --preview-model <item name>` shows a model on the title screen with
the kart's axes (red +X, green +Y, blue +Z) for checking its orientation.
