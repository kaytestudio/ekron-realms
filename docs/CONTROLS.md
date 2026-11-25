# Desktop controls and options

## Sandbox

| Input | Action |
| :--- | :--- |
| `W A S D` | Move player (or FPV drone throttle & yaw) |
| `Mouse` | Look around (or FPV drone pitch & roll; Arrow keys also supported) |
| `Space` / `C` / `Z` | Jump (ascend in flight) / Crouch (descend in flight) / Prone |
| `1` / `2` / `3` | Equip **Gravity Gun** / **FPV Remote** / **Empty Hands** |
| `Q` / `Tab` / `B` | Open or close Build & Equipment Catalog |
| `Left Click` | Grab/drop object (Gravity Gun), place selected item (Build), or connect FPV drone |
| `Right Click` / `F` | Freeze/unfreeze object or cancel Build preview |
| `R` / `Mouse Wheel` | Rotate build preview or held object by `90°` |
| `X` / `Delete` | Remove aimed placed object |
| `E` | Contextual interaction (doors, UAZ-469, sofa, FPV drone) |
| `V` / `H` | Toggle FPV/chase drone camera / Return FPV drone home |
| `N` / `P` / `F11` / `Esc` | Toggle `noclip` flight / Copy `spawn.json` / Fullscreen / Pause menu |
| `G` / `L` | Open the in-game store / Cycle UI language (English → Русский → Português → Español) |
| `` ` `` | Realms console (Quake 2 style: `set`, `seta`, `alias`, `exec`, `cvarlist`, `cmdlist`, `noclip`, `respawn`, `play`, `soundlist`, `s_volume`, `quit`…) |

## Gamepad

Any controller SDL knows (Xbox, PlayStation, Switch Pro, the built-in pad of a handheld such as the Anbernic RG40XXH) plays the whole game; a keyboard and mouse still work alongside it. The buttons are named in the Xbox layout (A = bottom, B = right, X = left, Y = top).

| Button | City | Quake 2 map |
| :--- | :--- | :--- |
| Left stick | Move (the d-pad too) | Move |
| Right stick | Look | Look |
| `A` | Jump (`Space`; up in flight) | Jump, held |
| `B` | Crouch (`C`; down in flight) | Crouch, held |
| `X` | Interact (`E`) | Use the selected item (`Enter`, `invuse`) |
| `Y` | Rotate (`R`) | Inventory (`Tab`, `inven`) |
| `RT` | Left click: grab / place / use the tool | Fire, held |
| `LT` | Right click: freeze / cancel | Jump, held |
| `LB` / `RB` | Previous / next tool (`1`-`3`) | Previous / next weapon (`weapprev` / `weapnext`) |
| D-pad | Move | Up: help computer (`F1`); down: scores (`score`); left / right: previous / next item (`[` / `]`) |
| `Back` + `LB` / `RB` | | Quick save / quick load (`F6` / `F9`) |
| `Start` | Pause menu (`Esc`), or close the catalog or store | Pause menu |

In the pause menu, the d-pad up and down selects a button, `A` presses it and `B` closes the menu. The build catalog and the store need the mouse; `B` closes them. The right stick turns at up to 2.8 radians a second at full tilt with the default `sensitivity` 3 (it scales with `sensitivity`, and `m_invert 1` inverts up and down), with a squared response for fine aim; both sticks ignore the inner 20% of their travel. The pad does nothing while the console is open. `SDL_GAMECONTROLLERCONFIG` adds mappings for pads SDL doesn't know.

### OpenGL ES

The client draws with OpenGL 3.3 Core, or OpenGL ES 3.0 where that is all the GPU offers (Mali, PowerVR and Adreno handhelds): it tries one, then the other. Builds made with `-DEKRON_GLES=ON` try OpenGL ES first; `EKRON_GLES=1` or `EKRON_GLES=0` in the environment picks the first try in any build. The handheld package is in [BUILD.md](../BUILD.md#handhelds-anbernic-rg40xxh-portmaster).

## Command line

```text
--version              Print client version and target architecture
--headless-smoke       Verify assets, GLB scenes, textures, physics, and save/load without opening a window
--assets <dir>         Override path to the shared Ekron assets directory
--online-host <host>   Connect to an experimental TLS presence relay (requires EKRON_JOIN_TOKEN env var)
--online-port <port>   Presence relay port (default: 38475)
--online-name <name>   Player display name (default: Player)
--lang <code>          UI language: en, ru, pt-PT or es-ES (also EKRON_LANG; default: saved choice, then system locale)
--no-intro             Start without the intro video (also EKRON_NO_INTRO=1)
--engine-info          Show which Realms core is active (Free Pascal or C++) and exit
+<command> [args]      Quake 2 style console commands, e.g. +set sensitivity 4 +exec my.cfg
--help                 Show CLI usage and controls
```

The `REALMS_CORE=pascal|native|auto` environment variable selects the Realms core. With `pascal`, startup fails if `librealms_core` is missing.

## Quake 2 maps

`map <name>` (console) or `+map <name>` (command line) opens `maps/<name>.bsp`, loose or from a `pak0.pak`, or a `.glb` / `.gltf` scene. `killserver` returns to the sandbox.

The apps ship one sample glTF map, `courtyard` (`map courtyard`, or `courtyard.glb` under **Maps…**). It has a walled yard under its own skybox, a bunker with a sliding door, a lift up to a platform whose button opens the vault, a pool, crates, barrels, items and weapons. It needs no Quake 2 files; without `.md3` models, items show as boxes. `tools/build_sample_map.py` rebuilds it, and it is a working example of the glTF rules below.

When the build finds `fpc` it also builds `librealms_game.so` / `.dylib`, the Free Pascal port of the Quake 2 game code in `game/`. `realms::GameServer` runs it, so BSP maps play as in Quake 2: spawn points, items, doors, lifts, triggers, monsters and weapons. Sounds come from your `pak0.pak`. Models are `.md3` files (see below); without them, entities are drawn as boxes. Maps built with `qrad3` use their lightmaps (`r_lightmaps 0` turns them off). With `pak0.pak` installed, map faces use their `.wal` textures; otherwise they get one of eight generic materials picked from the texture name. Faces draw their surface flags as Quake 2 does: water, slime and lava warp (`SURF_WARP`), conveyors scroll (`SURF_FLOWING`), and `SURF_TRANS33`/`SURF_TRANS66` faces such as water and glass are 33% or 66% opaque and drawn after the entities. Animated textures (texinfo `nexttexinfo` chains) step at 2 frames a second, or with the entity's frame on doors and buttons. The sky is the map's skybox (`env/`), turning when the map sets `skyrotate`.

glTF maps run the game too. Name nodes (Blender empties, for example) after Quake 2 point entities such as `info_player_start`, `item_health`, `weapon_shotgun`, `monster_soldier` or `misc_explobox`. Blender's `.001` suffixes are ignored. Put each one where Quake 2 would put its origin, about 0.75 m above the floor. Blender coordinates become Quake coordinates (32 units per metre), and an empty's Z rotation is its `angle`. Custom properties become entity keys (`spawnflags`, `targetname`, `angle`, ...) when the glTF export includes them, and the scene's custom properties go to `worldspawn`. A `sky` scene property (with `skyrotate` and `skyaxis` if wanted) draws that Quake 2 skybox, `env/<sky>rt.tga` and the other five faces, behind the whole scene; without it a glTF map keeps the plain sky. Doors, lifts and other brush entities work too: name the object that holds the door's meshes `func_door`, `func_plat`, `func_button`, `func_train`, `trigger_multiple` and so on, and its meshes and its children's meshes move and trigger as one inline model, as in a BSP map. Their keys come from custom properties only; for example, `angle` -1 opens a door upwards. A `func_rotating` turns about the object's origin. Triggers are invisible in the game. The full rules are in `realms/include/realms/MeshMap.h`.

| Input (in a map) | Action |
| :--- | :--- |
| `W A S D` / `Space` / `Ctrl` | Move / Jump / Crouch |
| `Left Click` | Fire the current weapon (with the mouse captured) |
| `N` / `R` | Noclip / Restart the map (respawn without the game) |
| `F1` | Help computer (Quake 2's `cmd help`): skill, level name, the map's objectives, kills, goals and secrets; `F1` again closes it. In the console, `help` does the same and `score` shows the scores |
| `Tab` / `[` `]` / `Enter` | Inventory (Quake 2's `inven`): open or close it, select the previous / next item (`invprev` / `invnext`), use the selected one (`invuse`) |
| `F6` / `F9` | Quick save / quick load (Quake 2's `save quick` / `load quick`) |
| `` ` `` | Console: Quake 2 client commands such as `give all`, `give shotgun`, `use blaster`, `god`, `notarget`, `kill`, `weapnext`, `cmd <command>`, server commands with `sv <command>`, and `killserver` |

- `sv_game 0`: walk maps without the game.
- `cl_gun 0`: hide the view weapon.
- The camera moves the way Quake 2's does: it bobs and tilts as you walk and strafe, flinches when you are hit, dips after a hard landing, kicks back when you fire, and after death it tips over to face the killer. The game's own cvars tune it: `bob_up`, `bob_pitch`, `bob_roll` (walking), `run_pitch`, `run_roll` (speed); `0` turns a part off.
- `fov 110`: the field of view in maps (default 90, 1 to 160), as in Quake 2: the angle across a 4:3 screen, so a wider screen shows more at the sides. The game gets it as userinfo, and a change applies at once. The city keeps its own view.
- The status bar (health, ammo, armor, the item just picked up, powerup timers) and the layout screens (help computer, scores, inventory) are drawn from the game's own layout programs; with `pak0.pak` installed they use its pics (icons, `pics/help.pcx`, the big digits), otherwise text and digits in the built-in font.
- `crosshair 0` hides the crosshair (in the city too). As in Quake 2, `1`, `2` or `3` picks the game's crosshair pic `pics/ch1.pcx` … `ch3.pcx`, drawn at the centre in maps while the game runs and `pak0.pak` is installed; any other value means `3`. Otherwise the client's own `+` is shown. The default is `1`, while Quake 2's is `0`. On desktop, a crosshair skin from the store replaces the game's pic.
- `cl_statusbar 0` (Ekron's own, not a Quake 2 cvar): hide the status bar and show health / armor / ammo as text in the map box instead.
- `gl_polyblend 0`: turn off the screen tint the game sets (red when hurt, gold on a pickup, blue with the Quad Damage, a colour under water); it is on by default, as in Quake 2.
- Music: the map's CD track (worldspawn `sounds`, which the game puts in the `CS_CDTRACK` config string) plays in a loop from Ogg Vorbis files named the way Yamagi Quake II looks for them: `music/trackNN.ogg` (the 2023 re-release), `music/TrackNN.ogg` (the GOG release) or `music/NN.ogg`, where `NN` is the track number with two digits (`02`…`11` for Quake 2). Each name is looked for in every [data place](#quake-2-data-baseq2) and its `baseq2/`, so GOG's `music` folder next to `baseq2` works where it lies. It plays at `s_musicvolume`, carries on with the pause menu open, and the city's own music waits while it plays. Track 0, track 1 (the CD's data track) or a missing file means no music.
- `REALMS_GAME_LIBRARY=/path/to/librealms_game.so`: use another game build.

### Saving

`save <name>` and `load <name>` in the console work as in Quake 2; `F6` and `F9` are `save quick` and `load quick`. A name is up to 32 letters, digits, `_` or `-`. Saving needs a running game and a living player. Loading opens the saved map again, then puts back the player, the monsters, items, doors and lifts as they were, with the skill the game was saved on.

- Saves are kept in the user data folder (see [Quake 2 data](#quake-2-data-baseq2)), as `save/<name>/`: the game's own `game.ssv` and `level.sav`, and `server.ssv` (the map, the skill, the config strings and the area portals), written last.
- A save is tied to the build of the game that wrote it. A save from another build is refused with a message, and the map then starts from the beginning.
- Like Quake 2's `load`, it restores one level: the levels visited before are kept only across the game's level changes (`gamemap`), which map mode doesn't run yet.

### Models (`.md3`)

Map mode draws Quake 3 `.md3` models (IDP3 version 15), not Quake 2's `.md2`. The game still names its models `models/<path>/tris.md2`; map mode loads `models/<path>/tris.md3` instead, loose or from a pak, in the same places as the rest of the data. Quake 2's own `.md2` files are never read, so with only `pak0.pak` installed, monsters, items and weapons are drawn as boxes until `.md3` versions with those names are added (a pak placed after `pak0.pak`, such as `baseq2/pak1.pak`, or loose files).

- Frames keep Quake 2's numbering: frame N of the `.md3` is the game's frame N (`stand01`, `run1`, ...), blended between game frames as before.
- Each surface is drawn with its own texture: the surface's shader name, a `.tga` or 8-bit `.pcx` path. When the named file is missing, the other of `.tga` / `.pcx` with the same base name is tried; a name without an extension is tried as `.tga`, then `.pcx`.
- An entity's skin number (pain skins, for example) picks the surface's shader with that index; a surface with fewer shaders uses its first.
- Texture coordinates repeat outside 0..1, as in Quake 3. Tags are read but not used.

### Quake 2 data (`baseq2`)

Copy the `baseq2` folder of a Quake 2 install, with its `pak0.pak`, into one of these places:

- the user data folder (the desktop's settings folder: `~/.local/share/KayteStudio/EkronRealms/baseq2/` on Linux, `~/Library/Application Support/KayteStudio/EkronRealms/baseq2/` on macOS, `%APPDATA%\KayteStudio\EkronRealms\baseq2\` on Windows);
- the assets folder (`<assets>/baseq2/`; on Android, `sys/android/app/src/main/assets/baseq2/` before building the APK; on iOS, add `baseq2` to the Xcode project as a folder reference like `maps`);
- anywhere else, then pass `--quake2 <dir>` or set `EKRON_QUAKE2_DIR=<dir>` (desktop). `<dir>` can be the Quake 2 folder or its `baseq2` folder.

The engine mounts `pak0.pak` … `pak9.pak` from each place, at the top and in `baseq2/`; later paks win, and paks win over loose files. A loose file such as `maps/mymap.bsp` is looked up as `<place>/maps/mymap.bsp` in every place first, then as `<place>/baseq2/maps/mymap.bsp`. A `pak0.pak` at the top of the assets folder still works. On desktop, `--engine-info` prints a `Quake 2:` line and the log prints `[Ekron] Quake 2 data:` at startup, listing the folders where paks were found or where to copy them. Put options such as `--quake2` before `+map`; an option after a `+command` ends that command.

### Android and iOS

**Pause menu → Maps…** lists the `.bsp`, `.glb` and `.gltf` files under `maps/` and `models/` in the app, plus imported maps. The APK and iOS device builds include the game; a Fire button appears above jump, and the pause menu has noclip, respawn and leave (`killserver`). The game's status bar is drawn at the bottom of the screen, and its help computer, scores and inventory over the view, as on the desktop. **Help** and **Items** buttons left of pause open and close the help computer and the inventory; while the inventory is open, a row under them selects the previous or next item and uses the selected one. With the game running, the pause menu also has **Quick save** and **Quick load** (the same `quick` save as `F6` / `F9` on the desktop).

**Import map…** copies files from the system picker into the app's `maps/` folder (Android internal storage, iOS `Application Support/EkronSandbox`). Pick a `.gltf` together with its `.bin` and texture files; folders are not kept. Accepted: `.bsp .glb .gltf .bin .png .jpg .jpeg`, up to 128 MB each, checked by file header. **Remove an imported map…** deletes one.

More detail in [realms/README.md](../realms/README.md#the-quake-2-module-realms_quake2).

## realms-pascal

With `fpc` and `-DENABLE_FPC_CLIENT=ON` the build also produces `realms-pascal` (`Projects/realms.lpr`): the Pascal core linked into a Free Pascal program that runs the C++ sandbox from `libekron_sandbox`. It takes the same flags as `ekron-realms`. It can also be built with `lazbuild Projects/realms.lpi`, `make pascal` or `fpc @realms-fpc.cfg Projects/realms.lpr`; see [BUILD.md](../BUILD.md#free-pascal-client-realms-pascal).
