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

When the build finds `fpc` it also builds `librealms_game.so` / `.dylib`, the Free Pascal port of the Quake 2 game code in `game/`. `realms::GameServer` runs it, so BSP maps play as in Quake 2: spawn points, items, doors, lifts, triggers, monsters and weapons. Models, skins and sounds come from your `pak0.pak`; without it, entities are drawn as boxes. Maps built with `qrad3` use their lightmaps (`r_lightmaps 0` turns them off). With `pak0.pak` installed, map faces use their `.wal` textures; otherwise they get one of eight generic materials picked from the texture name. Faces draw their surface flags as Quake 2 does: water, slime and lava warp (`SURF_WARP`), conveyors scroll (`SURF_FLOWING`), and `SURF_TRANS33`/`SURF_TRANS66` faces such as water and glass are 33% or 66% opaque and drawn after the entities. Animated textures (texinfo `nexttexinfo` chains) step at 2 frames a second, or with the entity's frame on doors and buttons. The sky is the map's skybox (`env/`), turning when the map sets `skyrotate`.

glTF maps run the game too. Name nodes (Blender empties, for example) after Quake 2 point entities such as `info_player_start`, `item_health`, `weapon_shotgun`, `monster_soldier` or `misc_explobox`. Blender's `.001` suffixes are ignored. Put each one where Quake 2 would put its origin, about 0.75 m above the floor. Blender coordinates become Quake coordinates (32 units per metre), and an empty's Z rotation is its `angle`. Custom properties become entity keys (`spawnflags`, `targetname`, `angle`, ...) when the glTF export includes them, and the scene's custom properties go to `worldspawn`. Doors, lifts and other brush entities work too: name the object that holds the door's meshes `func_door`, `func_plat`, `func_button`, `func_train`, `trigger_multiple` and so on, and its meshes and its children's meshes move and trigger as one inline model, as in a BSP map. Their keys come from custom properties only; for example, `angle` -1 opens a door upwards. A `func_rotating` turns about the object's origin. Triggers are invisible in the game. The full rules are in `realms/include/realms/MeshMap.h`.

| Input (in a map) | Action |
| :--- | :--- |
| `W A S D` / `Space` / `Ctrl` | Move / Jump / Crouch |
| `Left Click` | Fire the current weapon (with the mouse captured) |
| `N` / `R` | Noclip / Restart the map (respawn without the game) |
| `` ` `` | Console: Quake 2 client commands such as `give all`, `give shotgun`, `use blaster`, `god`, `notarget`, `kill`, `weapnext`, `cmd <command>`, server commands with `sv <command>`, and `killserver` |

- `sv_game 0`: walk maps without the game.
- `cl_gun 0`: hide the view weapon.
- `REALMS_GAME_LIBRARY=/path/to/librealms_game.so`: use another game build.

### Android and iOS

**Pause menu → Maps…** lists the `.bsp`, `.glb` and `.gltf` files under `maps/` and `models/` in the app, plus imported maps. The APK and iOS device builds include the game; a Fire button appears above jump, and the pause menu has noclip, respawn and leave (`killserver`).

**Import map…** copies files from the system picker into the app's `maps/` folder (Android internal storage, iOS `Application Support/EkronSandbox`). Pick a `.gltf` together with its `.bin` and texture files; folders are not kept. Accepted: `.bsp .glb .gltf .bin .png .jpg .jpeg`, up to 128 MB each, checked by file header. **Remove an imported map…** deletes one.

More detail in [realms/README.md](../realms/README.md#the-quake-2-module-realms_quake2).

## realms-pascal

With `fpc` and `-DENABLE_FPC_CLIENT=ON` the build also produces `realms-pascal` (`Projects/realms.lpr`): the Pascal core linked into a Free Pascal program that runs the C++ sandbox from `libekron_sandbox`. It takes the same flags as `ekron-realms`. It can also be built with `lazbuild Projects/realms.lpi`, `make pascal` or `fpc @realms-fpc.cfg Projects/realms.lpr`; see [BUILD.md](../BUILD.md#free-pascal-client-realms-pascal).
