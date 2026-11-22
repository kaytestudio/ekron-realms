# Realms Engine

Realms is the engine layer under the Ekron sandbox. It takes the Quake 2 engine
stack (the Free Pascal/Delphi port in [`game/qcommon`](../game/qcommon)) and puts
it behind one C ABI. The C++ sandbox uses that ABI on **Desktop (Linux/macOS),
Android and iOS**.

```
 ┌───────────── sandbox (C++17, per platform) ─────────────┐
 │ sys/desktop/main.cpp   Renderer.cpp (Android)   SandboxBridge.mm (iOS) │
 └───────────────┬─────────────────────────────────────────┘
                 │ realms::Engine  (realms/include/realms/Engine.h)
                 │ realms::sandbox (SandboxBoot.h: shared cvars + boot order)
 ┌───────────────┴──────────── C ABI ──────────────────────┐
 │ realms_bridge.h: RealmsHostApi  ⇄  RealmsCoreApi (v3)   │
 └───────┬──────────────────────────────────┬──────────────┘
         │ dlopen / static link             │ built in
 ┌───────┴───────────────┐        ┌─────────┴────────────┐
 │ librealms_core         │        │ NativeCore.cpp        │
 │ (rlm_*.pas)            │        │ NativeCModel.cpp      │
 │ Free Pascal            │        │ C++ reference core    │
 │ "pascal-quake2"        │        │ "native-cpp"          │
 └────────────────────────┘        └───────────────────────┘
```

## What this foundation provides

These subsystems are ported from Quake 2 `qcommon` in both cores, and both cores behave the same way:

| Subsystem | Quake 2 origin | Pascal unit |
| :--- | :--- | :--- |
| Console output (`Com_Printf`/`Com_DPrintf`) is routed to stdout, logcat, NSLog and the in-game console | `common.c` | `rlm_console.pas` |
| Console variables: ARCHIVE, USERINFO, SERVERINFO, NOSET, LATCH, plus Realms' CHEAT flag | `cvar.c` | `rlm_cvar.pas` |
| Command buffer, tokenizer, `$macro` expansion, aliases, `wait`, `exec`, `set`/`seta`/`toggle`/`reset`, `cvarlist`, `cmdlist`, `writeconfig` | `cmd.c`, `cvar.c` | `rlm_cmd.pas` |
| Virtual filesystem: `pak0..9.pak` mounted over loose files, read with range reads on mobile | `files.c` | `rlm_files.pas` |
| Collision model: Quake 2 IBSP v38 maps loaded through the VFS, point contents, box/point traces, inline models (`*1`…), temporary box hulls, origin/angle transformed queries, leafs | `cmodel.c` | `rlm_cmodel.pas` |
| Visibility and areas: PVS/PHS rows, area portals and flood-filled area connectivity, `CM_BoxLeafnums`, `CM_HeadnodeVisible` | `cmodel.c` | `rlm_cmodel.pas` |
| Triangle-mesh maps (Realms extension): glTF levels turned into brush collision with a kd tree | - | `rlm_cmodel.pas` |

Shared C++ on top of the ABI (`realms/src`, used by every platform): the glTF map converter (`MeshMap`), drawable map geometry (`MapGeometry`), Quake 2 player movement (`MapWalker`) the `map` / `mapinfo` / `killserver` commands (`SandboxBoot`), and the Quake 2 server that hosts the game module (`GameServer`, with `Q2Pmove`, a port of `pmove.c`). See [Quake 2 game module](#quake-2-game-module-gameserver).

## Maps and collision (bridge v2)

`map_load("maps/base1.bsp")` reads the map through the VFS, so it can come from a mounted PAK or a loose file. The loader checks every lump and index before it uses them:

- header, lump ranges and record sizes;
- the Quake 2 `MAX_MAP_*` limits;
- plane, brush side, texinfo, leaf brush, node child and model headnode indices;
- leaf 0 must be solid and the map needs an empty leaf;
- node graphs with cycles, or deeper than 1024 levels, are rejected.

A map that fails a check is rejected with a console warning (`map_load: maps/x.bsp: bad node child`) and leaves no map loaded, so a bad download can't crash the core. The traces are Quake 2's `CM_BoxTrace` / `CM_TransformedBoxTrace`, including `DIST_EPSILON` and the start == end position test, so game code ported from Quake 2 gets the same answers.

```cpp
engine.loadMap("maps/base1.bsp");
realms::TraceResult tr = engine.boxTrace(start, end, {-16, -16, -24}, {16, 16, 32}, 0, REALMS_MASK_PLAYERSOLID);
if (tr.hit()) { /* tr.endpos, tr.normal, tr.surface, tr.contents */ }
realms::MapModel door;
engine.mapModel(1, door); // "*1": trace it with transformedBoxTrace(..., door.headnode, mask, origin, angles)
int32_t water = engine.pointContents(eye) & REALMS_MASK_WATER;
```

`map_info` reports the CRC-32 of the `.bsp` (for client/server map checks), the lump counts and the world bounds. The legacy `game/qcommon/CModel.pas` is kept as the reference; `rlm_cmodel.pas` follows it with dynamic arrays and 64-bit-safe indices instead of `Integer(pointer)` casts, and with `Com_Error` exits replaced by the rejection path above.

## Backend selection

`realms::Engine::init` picks a core like this:

1. `EngineConfig::pascalLibrary`, then `REALMS_CORE_LIBRARY`.
2. Then it looks next to the executable:
   - `librealms_core.so`/`.dylib` in the same directory;
   - `../Frameworks/` for the macOS `.app`;
   - `../lib/ekron-realms/` for the `.deb`;
   - the bare name, which covers Android `jniLibs`.
3. If no Pascal core is found, it uses the built-in C++ reference core.

You can override the choice with `REALMS_CORE=pascal|native|auto`. With `pascal`, startup fails if the library is missing.
`ekron-realms --engine-info` shows which core is running.

## Visibility, area portals and mesh maps (bridge v3)

Bridge v3 adds nine functions to `RealmsCoreApi`, implemented in both cores:

| Function | Quake 2 | Notes |
| :--- | :--- | :--- |
| `map_cluster_pvs`, `map_cluster_phs` | `CM_ClusterPVS/PHS` | Decompressed rows of `(numclusters + 7) / 8` bytes. A map without a visibility lump sees everything; a bad cluster gives an empty row. |
| `map_set_area_portal`, `map_area_portal_open` | `CM_SetAreaPortalState` | Portals start closed when a map loads, as in Quake 2 (doors open them). |
| `map_areas_connected`, `map_write_area_bits` | `CM_AreasConnected`, `CM_WriteAreabits` | Areas are re-flooded when a portal changes. |
| `map_box_leafs`, `map_headnode_visible` | `CM_BoxLeafnums`, `CM_HeadnodeVisible` | Entity culling for a future server. |
| `map_load_mesh` | - | Loads a triangle soup as the current map (below). |

`Engine` wraps them as `clusterPVS`, `clusterPHS`, `setAreaPortal`, `areasConnected`, `areaBits`, `boxLeafs` and `headnodeVisible`. `Engine::parseEntities` / `entityValue` parse the entity lump with Quake 2's `ED_ParseEdict` syntax.

### glTF maps

`engine.loadMap("maps/street.glb")` (or `.gltf`) loads a glTF 2.0 level as a collision map:

- `realms::gltfToMeshMap` (`MeshMap.h`) reads `.glb` and `.gltf` itself, with no third-party code. It handles external and `data:` buffers, the node hierarchy with matrix or TRS transforms, instancing and mirrored nodes. Hostile files hit size and count limits rather than reading out of bounds; it survives a 3000-iteration mutation fuzz.
- Axes and units: glTF (x, y, z) metres become Quake 2 (x, -z, y) × `map_mesh_scale` (default 32 units per metre). That puts a 1.75 m player at Quake 2's 56 units.
- Every triangle becomes a slab brush (`map_mesh_thickness`, default 2 units) with bevel planes. The core sorts the brushes into a kd tree, so traces use Quake 2's `CM_BoxTrace` unchanged and hit the same `DIST_EPSILON` contact points as on a BSP.
- Materials become surfaces. A material name containing `nocollide`, `water`, `slime` or `lava` sets its contents, and material `extras` can set `realms_contents`, `realms_flags` and `realms_value` directly. A node whose name contains `nocollide` is drawn but has no collision.
- Nodes named `info_player_start`, `info_player_deathmatch` and so on become entities. Their origin is the node's translation, and `realms_angle` in its extras sets the facing. `worldspawn` is generated.

### Map commands (every platform)

`realms::sandbox::boot()` registers these by default (`BootOptions::mapCommands`):

| Command | Effect |
| :--- | :--- |
| `map <name>` | Loads `<name>`, `maps/<name>.bsp`, `.glb` or `.gltf`, or any VFS path with an extension (`map models/abandoned_city_street_for_games.glb`). Sets the read-only `mapname` cvar and calls `BootOptions::onMapChanged(path)`. |
| `mapinfo` | Lump counts, bounds, checksum, mesh triangles and surfaces, and entity class counts. |
| `killserver` | Unloads the map and calls `onMapChanged("")`. |

### Desktop map mode

On desktop, `map` swaps the sandbox for the loaded map (`sys/desktop/DesktopMapMode.*`). The renderer-independent part is `mapview::MapSession` (`sys/android/app/src/main/cpp/MapSession.*`), shared with Android: the map-to-render space conversion, the camera conventions, material layers and the walker.

- **Movement:** you walk with `realms::MapWalker`, a port of Quake 2's pmove (`PM_StepSlideMove`, friction, acceleration, 18-unit steps, ducking, swimming, jumping). It runs on `Engine::boxTrace` against whichever core is active.
- **Game:** when the game module is available and the `sv_game` cvar is 1 (the default, archived), the map runs the Quake 2 game through `realms::GameServer`: `info_player_start`, items, doors, platforms, triggers and monsters behave as in Quake 2, and the player moves with the game's own `Pmove`. Set `sv_game 0` (then `map` again) to only walk the map with `MapWalker`. The HUD shows health, ammo and armour; centre prints and pickup messages become toasts. Inline brush models (`*1`, `*2`…, doors and lifts) are drawn where the game puts them, interpolated between game frames. Items, monsters, projectiles and the view weapon are drawn from their `.md2` models and PCX skins when the VFS has them (a Quake 2 `pak0.pak`); without id's data each entity is a box the size of its bounds (gold items, red monsters, orange projectiles), so the game stays playable. `cl_gun 0` hides the view weapon.
- **Controls:** WASD, Space to jump, Ctrl to crouch, N for noclip. With the game: left mouse fires (mouse captured), R restarts the map, and the console forwards Quake 2 client commands (`give all`, `give shotgun`, `use blaster`, `god`, `notarget`, `kill`, `weapnext`, `inven`…, or `cmd <anything>`) and `sv <command>` server commands. Without it R respawns. `killserver` returns to the sandbox where you left it.
- **Spawn:** `MapWalker::spawn` uses `info_player_start`, then the deathmatch and coop spots. glTF scenes rarely have any of these, so for maps without them it searches the map. It scans a 24 × 24 grid of columns for every walkable floor and takes the map's ground level: the lowest height that a tenth of the columns share, so streets beat rooftops and a room's floor beats its roof. On that level it picks the spot with the longest walkable runs in 8 directions. A run ends at a wall or a drop, and it needs floor under the player's centre, so the void past a map's rim and the tops of walls don't count as open. The search is deterministic and uses only box traces, so both cores pick the same spot. The street .glb takes about 0.3 s. Maps with no walkable floor fall back to the map's centre (or top).
- **BSPs** are drawn from their face lump (sky and nodraw skipped). Maps without faces are drawn from their brush windings. Texture names pick the sandbox material layer (brick, metal, wood, glass, water…).
- **glTF maps** are drawn with the textured model through the regular glTF pipeline, in the file's own space. `tests/realms/map_render_alignment_test.cpp` (CTest `realms_map_render_alignment`) checks that the drawn model and the collision coincide for every `.glb` shipped in `assets/models`.

### Quake 2 game module (GameServer)

`game/` is the Free Pascal port of Quake 2's game DLL (gamex86: `g_*`, `m_*` monsters, `p_*` player code). `game/realms_game.lpr` builds it as `librealms_game.so` / `.dylib`, which exports `GetGameAPI` with the unchanged **Quake 2 game API 3** C ABI (`realms/include/realms/q2game.h`). The game units use `{$PACKRECORDS C}`, so `edict_t`, `gclient_t`, `trace_t`, `pmove_t` … have the C layouts; `q2game.h` and `GameUnit` check the sizes on both sides.

`realms::GameServer` (`realms/src/GameServer.cpp`) is the server half of Quake 2 (`sv_game.c`, `sv_world.c`, parts of `sv_init.c` / `sv_user.c`) on top of `realms::Engine`:

- `game_import_t` traces, point contents, PVS / PHS and area portals go through the Engine, so the game behaves the same on the Pascal and the C++ core (`SV_Trace` clips against the world, inline models and entity boxes);
- `SV_LinkEdict` / `SV_AreaEdicts` with area nodes, config strings, model / sound / image indexes, Quake 2 `cvar_t` records mirrored from the Engine's cvars, tag memory, the command tokenizer;
- one local client: `ClientConnect` / `ClientBegin`, one usercmd per sandbox frame (`ClientThink`, i.e. `Pmove`, using `Q2Pmove`), `RunFrame` at 10 Hz;
- sounds, prints, centre prints and temp entities from the game are queued as `GameEvent`s (`takeEvents`); game errors stop the server instead of the process.

| How the module is found | |
| :--- | :--- |
| `GameServer::setLinkedGame(GetGameAPI)` | a program that compiles `game/` in |
| `GameServerConfig::library` | an explicit path |
| `REALMS_GAME_LIBRARY` | environment override (a path) |
| next to the executable, `../Frameworks/` (macOS app), `../lib/ekron-realms/` (`.deb`) | `librealms_game.so` / `.dylib` |

`--engine-info` prints the module it would use (`Game: …/librealms_game.so (Quake 2 game API 3)`). The desktop build adds the `realms_game_module` target when `fpc` is installed (`-DENABLE_GAME_MODULE=ON`, the default); `make game`, `lazbuild game/realms_game.lpi` or

```bash
cd game && fpc -Mdelphi -dGAMEDLL -Cg -O2 -FUunits -Fu. -Fi. -o../build/librealms_game.so realms_game.lpr
```

build it by hand. Android and iOS don't build the game module yet; their map mode walks maps with `MapWalker`.

#### Drawing the game (`GameView`, `Md2Model`)

What a Quake 2 client does with the server's frames lives in `realms/` too, with no renderer types, so other front ends can reuse it:

- `GameServer::renderEntities()` lists the entities a client would receive (`SV_BuildClientFrame` minus the PVS cull): model name, origin, angles, frame, skin, effects, renderfx, bounds. `lerpFraction()` says how far the sandbox is into the next 100 ms frame.
- `GameEntityLerp` (`realms/include/realms/GameView.h`) keeps the last two game frames and interpolates like `CL_AddPacketEntities`:
  - origin and angles (`LerpAngle`), with oldframe / backlerp for the animation;
  - no lerp for new, re-modelled or teleported (> 512 units) entities;
  - `EF_ROTATE` autorotation and the `EF_ANIM*` client-side frames;
  - the view weapon (`ps.gunindex`, `gunframe`, `gunoffset`, `gunangles`).
- `buildGameScene` turns that into triangle batches through `GameModelCache` (`.md2` from the VFS, failures remembered and reported once). Each batch has its skin and tint (`RF_SHELL_*`, `RF_TRANSLUCENT`) and is placed like `R_RotateForEntity`, with the alias-model pitch negation. Models that are missing get a coloured box.
- `Md2Model` parses IDP2 v8 models (`Mod_LoadAliasModel` limits, every offset and index checked) and blends two frames into a triangle list with normals, always counter-clockwise from outside. `decodePcx` decodes the 8-bit RLE skins.

On desktop, `DesktopMapMode::buildEntities` uploads the skins once, converts the vertices to render space (`sys/desktop/MapEntityTransform.h`) and hands the batches to `DesktopRenderer::setMapEntities`. The renderer draws them after the map with the textured model shader: opaque first, then translucent, then the view weapon over a cleared depth buffer (Quake 2's `RF_DEPTHHACK`).

### Android and iOS map mode

On both phones, **pause menu → Maps…** lists every `.bsp`, `.glb` and `.gltf` under `maps/` and `models/` in the app package, plus `maps/` in the app's writable storage:
- **Android:** internal storage (`filesDir`).
- **iOS:** `Application Support/EkronSandbox`, the engine's first search root.

Picking one runs `map "<path>"`; any other `map` command (a script, `autoexec.cfg`) switches over the same way. Names from the pickers go through `mapview::safeMapName` before they reach the command line.

**Import map…** at the bottom of the list opens the system file picker (Android `OpenMultipleDocuments`, iOS `UIDocumentPickerViewController`). The picked files are copied into that writable `maps/` folder and the first map among them loads.
- **Several files at once:** pick a `.gltf` together with its `.bin` buffers and `.png` / `.jpg` textures. The copies are flat, so URIs into subfolders won't resolve.
- **Shared rules** (`mapview::importFileName` / `importHeaderOk`, tested in `realms_map_render_alignment`):
  - names are cleaned to the `safeMapName` character set;
  - only `.bsp .glb .gltf .bin .png .jpg .jpeg` are accepted;
  - the first bytes must match the type (`IBSP`, `glTF`, a JSON object, PNG / JPEG signatures);
  - files are capped at 128 MB.
- **Safe copies:** each copy goes through a `.part` file, so a cancelled or oversized import never leaves a broken map.
- **Removing:** **Remove an imported map…** deletes one, except the map being walked.

Where the code lives:
- **Android:** `sys/android/app/src/main/cpp/AndroidMapMode.*`, with hooks in `Renderer.cpp` and `MainActivity.kt`.
- **iOS:** `MetalMapMode` in `sys/ios/realms/realms iOS/SandboxBridge.mm` is the Metal twin of `AndroidMapMode`. The bridge exposes `loadMap(_:)`, `currentMapPath()` and status slot 17, and the UI is in `GameViewController.swift`.
- **Both:** the shared `MapSession.cpp`.

- **Controls:** the move stick and look area walk with `realms::MapWalker`. Jump and crouch work as in the sandbox. The map's pause menu toggles noclip (↑ / ↓ for altitude), respawns, opens another map, or leaves the map (`killserver`).
- **HUD:** the sandbox tools, catalogue and tool wheel hide while you're on a map. The sandbox world stays as it was and comes back when you leave.
- **Drawing:**
  - **The sandbox street:** when the map is that file, the city model already on the GPU is reused rather than a second 14 MB copy. That model is a rescaled, recentred "level" scene. `city::Scene` records how it relates to the file (`sourceScale`, `sourceOffset`), and the camera moves into that space. `realms_map_render_alignment` checks the relation, vertex by vertex, on every shipped `.glb`.
  - **Other glTF maps:** the textured file is prepared on a worker thread and uploaded a few steps per frame. Until then the map's own triangles are drawn.
  - **BSPs:** the map's triangles with the sandbox material layers.
- **Lifecycle:** if Android recreates the EGL surface (or iOS recreates the bridge), the map stays loaded in the engine and map mode reopens where you were standing.
- **Failure:** if a map fails to load, the sandbox stays and shows "The map could not be loaded."

## Shared boot (every platform)

`realms::sandbox::boot()` runs the same steps on every platform:

1. Registers `sensitivity`, `m_invert`, `name`, and `realms_platform`. `realms_platform` is read-only and holds `desktop-linux`, `desktop-macos`, `android` or `ios`.
2. Mounts `pak0.pak` … `pak9.pak`.
3. Executes `config/realms.cfg`, then `config.cfg` (archived cvars, saved with `writeconfig`), then `autoexec.cfg`.
4. Registers the map commands (`map`, `mapinfo`, `killserver`) and the `map_mesh_scale` / `map_mesh_thickness` cvars.
5. Runs `+command` arguments from the command line (desktop only; `+map base1` works).

Mouse and touch look use `sensitivity` and `m_invert` on all three platforms.

## Building the Pascal core

| Platform | How |
| :--- | :--- |
| Linux / macOS | The desktop CMake build adds the `realms_pascal_core` target when `fpc` is installed. It is controlled by `-DENABLE_PASCAL_CORE=ON`, which is the default. |
| Android | Free Pascal 3.2 has no packaged Android cross compiler, so build one once: `realms/pascal/fpc_android_toolchain.sh <FPCSource-release_3_2_2> $HOME/fpc-android aarch64 x86_64`. It needs a host `fpc` and GNU binutils (`binutils-aarch64-linux-gnu`). Then `FPC_ANDROID_PREFIX=$HOME/fpc-android ANDROID_NDK_HOME=… realms/pascal/build_core.sh android-aarch64` writes `librealms_core.so` to `sys/android/app/src/main/jniLibs/arm64-v8a/`. It links against bionic (API 30) with 16 KB page alignment. CI does all of this and packages the arm64-v8a and x86_64 cores into the APK. |
| iOS | Build the aarch64-ios runtime library once on an Apple Silicon Mac. It uses the Homebrew `fpc` plus FPC sources of the same version: `realms/pascal/fpc_ios_toolchain.sh <FPCSource-release_3_2_2> $HOME/fpc-ios`. Then `FPC_IOS_PREFIX=$HOME/fpc-ios realms/pascal/build_core.sh ios-arm64` writes `build/realms-ios/librealms_core.a` and `RealmsPascal.generated.xcconfig`. The Xcode project includes that file through `sys/ios/realms/RealmsPascal.xcconfig`, so device builds link the Pascal core and define `REALMS_PASCAL_STATIC=1` with no further setup. Simulator builds, and builds without the file, use the C++ core. The archive is a single prelinked object, so the `__mod_init_func` entry that starts the FPC RTL always comes along. Link it with `-Wl,-no_fixup_chains`, because FPC 3.2 tables contain pointers that are only 4-byte aligned. |
| Any CMake consumer | `-DREALMS_PASCAL_STATIC_LIBRARY=/path/librealms_core.a` links a static Pascal core into `realms_engine`. |
| A Pascal program | `realms_core.lpr` keeps the API table in `rlm_core_api.inc`, so a Free Pascal program can compile the core in and hand `realms_pascal_get_api` to `realms::Engine::setLinkedPascalCore()` (origin `linked`). `Projects/realms.lpr` (`realms-pascal`) does this and runs the C++ sandbox from `libekron_sandbox`. |

## Tests

`tests/realms/realms_bridge_test.cpp` is the ABI conformance suite. It covers cvar semantics, the command buffer, aliases and macros, PAK mounting, `writeconfig`, error paths, the collision model and the Engine facade.

The map checks (`tests/realms/realms_map_suite.h`) need real BSP files, and no id Software map can ship with the repository. `tests/realms/bsp_builder.h` therefore compiles a test level from boxes into a genuine IBSP v38 file: BSP tree, solid leaf 0, clusters, areas, brush sides and texinfo. The level has a floor, a wall, a water pool, a player-clip brush and a door (`*1`). The suite checks exact trace fractions and end positions, surfaces, masks, transforms, box hulls and leafs. It also checks that 18 kinds of corrupt map are rejected and survives 400 randomly corrupted copies.

The other suites:

- `realms_vis_mesh_suite.h` covers PVS/PHS, area portals, box leafs and mesh maps in the core.
- `realms_meshmap_suite.h` covers the glTF converter, using `gltf_builder.h` to write `.glb` / `.gltf` files in memory.
- `game_server_test.cpp` (CTest `realms_game_server_native` / `_pascal`) checks `Q2Pmove` on Engine traces (standing, walking, steps, walls, jumping, water), then loads the real `librealms_game` on a generated level, on both cores: spawning on `info_player_start`, walking through `ClientThink` / `Pmove`, picking up an armour shard, the door (`*1`) opening when the player walks into its trigger, server-side traces and per-model geometry, `god` / `give` / `notarget` and the forwarded console commands, weapon fire, game events, latched cvars, and stopping and restarting the server. It then checks what the client draws, on both cores:
  - the render entity list (never the player's own body);
  - interpolation of the rising door, which is always between the last two game frames and strictly between them partway;
  - the shard from a generated `.md2` with its PCX skin, and its fallback box once the model is gone;
  - the missing blaster model, reported once;
  - the shotgun view model following the camera, and hidden by `cl_gun 0`. CTest `ekron_desktop_game_module` checks that `--engine-info` finds the module.
- `md2_test.cpp` (CTest `realms_md2`) checks `.md2` parsing, frame blending, normals and winding for both windings, and texel-centred skin coordinates. It also rejects 23 kinds of corrupt model and survives 2,000 randomly corrupted ones, then checks PCX decoding (runs, padded lines, palette, transparency) and corrupt PCX files. `md2_builder.h` writes the models and images.
- `map_entity_transform_test.cpp` (CTest `realms_map_entity_transform`) checks that, through the desktop's transform and vertex conversion, the view weapon points along the camera's forward, up and left axes for every yaw and pitch, at 1 m per 32 units. It also checks brush-model pitch, translation, yaw and winding.
- `realms_walker_suite.h` covers map geometry from the face lump and from brush windings, with error paths and a fuzz. It also walks a test level through the Engine on both cores: stepping up a 16-unit step, being stopped by a 40-unit block, sliding along walls, player clip, jump height, crouching under a low ceiling, swimming, noclip, and spawning on BSP and glTF maps: it must find open ground beside a hut that encloses the map's centre, and fall back when there is no floor.

All of these run against:

- the C++ core (CTest `realms_bridge_native`; Android/iOS CI);
- the Pascal core (CTest `realms_bridge_pascal`, i.e. `realms_bridge_test --pascal <lib>`, when `fpc` is available);
- both cores on an Android x86_64 emulator (API 30), using the Android-built `librealms_core.so` (`realms/pascal/android_device_test.sh`, CI job `realms-pascal-device`).
- the real debug APK on that emulator (`realms/pascal/android_app_smoke.sh`, CI job `realms-apk-smoke`). The app must log `Realms engine ready: core=pascal-quake2` and keep running.
- the statically linked Pascal core on macOS arm64. This is `build_core.sh macos-arm64-static` plus `realms_bridge_test --pascal static` built with `-DREALMS_PASCAL_STATIC`, the same packaging as iOS. CI then builds the iOS device app and requires it to link `librealms_core.a`.

On Linux and macOS, CI also starts the packaged sandbox with `REALMS_CORE=pascal --headless-smoke`. The run must report `realmsCore=pascal-quake2`. It does the same with the `realms-pascal` program, whose `--engine-info` must report `Origin:   linked` (CTest `realms_pascal_client_engine_info` / `_headless_smoke`). The bridge suite also checks `setLinkedPascalCore()`: on the Pascal core, Pascal and Auto engines use the linked table; on the C++ run, a linked core that rejects the bridge version is reported and Auto falls back.

```bash
g++ -std=c++17 -Wall -Wextra -Werror -pedantic -Irealms/include \
  tests/realms/realms_bridge_test.cpp realms/src/{Engine,NativeCore,NativeCModel,SandboxBoot,MeshMap,MapGeometry,MapWalker}.cpp \
  -ldl -o /tmp/realms-test
/tmp/realms-test                                  # C++ reference core
/tmp/realms-test --pascal build/librealms_core.so # Free Pascal core
```

## ABI rules

- Memory is freed by the side that allocated it:
  - host blobs with `host->free_file`;
  - core blobs with `core->fs_free`;
  - strings are copied into buffers the caller provides.
- Every Pascal export is wrapped in `try/except`, so no exception crosses into C++. Floating-point exceptions are masked on entry.
- The C++ core is compiled without fused multiply-add contraction (`#pragma clang fp contract(off)` in every `realms/src` file, plus `-ffp-contract=off` in CMake). FPC never fuses, so the C++ and Pascal cores give bit-identical trace results on arm64 and on FMA-capable x86 too.
- `rlm_bridge.pas` mirrors `realms_bridge.h` field for field (`{$PACKRECORDS C}`, `cdecl`). If you change one, change the other and bump `REALMS_BRIDGE_VERSION`.
