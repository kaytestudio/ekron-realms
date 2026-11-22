# Realms Engine

Realms is the engine layer under the Ekron sandbox: the Quake 2 `qcommon` code
(from the Free Pascal port in [`game/qcommon`](../game/qcommon)) behind one C
ABI, [`realms_bridge.h`](include/realms/realms_bridge.h), used by the desktop,
Android and iOS hosts through `realms::Engine`.

There are two implementations of the ABI that behave the same and are tested
against one suite:

- `librealms_core` (`realms/pascal/rlm_*.pas`), Free Pascal, reported as `pascal-quake2`;
- `NativeCore.cpp` / `NativeCModel.cpp`, the C++ reference core, reported as `native-cpp`.

## Core subsystems

| Subsystem | Quake 2 | Pascal unit |
| :--- | :--- | :--- |
| Console output to stdout, logcat, NSLog and the in-game console | `common.c` | `rlm_console.pas` |
| Cvars (ARCHIVE, USERINFO, SERVERINFO, NOSET, LATCH, plus CHEAT) | `cvar.c` | `rlm_cvar.pas` |
| Command buffer, `$macro`s, aliases, `wait`, `exec`, `set`/`seta`/`toggle`/`reset`, `writeconfig` | `cmd.c`, `cvar.c` | `rlm_cmd.pas` |
| Filesystem: `pak0..9.pak` over loose files | `files.c` | `rlm_files.pas` |
| Collision: IBSP v38 maps, traces, point contents, inline models, box hulls | `cmodel.c` | `rlm_cmodel.pas` |
| PVS/PHS, area portals, `CM_BoxLeafnums`, `CM_HeadnodeVisible` (bridge v3) | `cmodel.c` | `rlm_cmodel.pas` |
| glTF triangle meshes as brush collision (Realms addition, bridge v3) | - | `rlm_cmodel.pas` |

`map_load` checks every lump, index and limit before use and rejects bad maps
with a console warning, leaving no map loaded. Traces match `CM_BoxTrace`,
including `DIST_EPSILON`.

```cpp
engine.loadMap("maps/base1.bsp");
realms::TraceResult tr = engine.boxTrace(start, end, {-16, -16, -24}, {16, 16, 32}, 0, REALMS_MASK_PLAYERSOLID);
int32_t water = engine.pointContents(eye) & REALMS_MASK_WATER;
```

glTF maps (`.glb`, `.gltf`) are converted by `realms::gltfToMeshMap`
(`MeshMap.h`): 32 units per metre by default (`map_mesh_scale`), each triangle
a 2-unit slab brush (`map_mesh_thickness`). Material names and extras set
contents. Nodes named after Quake 2 point entities (`info_player_start`,
`item_*`, `weapon_*`, `monster_*`, ...) become entities, with their extras as
keys, so the game runs on glTF maps too. Brush entities are not exported
(mesh maps have no inline models). See the header for the tags and entity
rules.

## Choosing a core

`realms::Engine::init` tries, in order:

1. `EngineConfig::pascalLibrary`, then `$REALMS_CORE_LIBRARY`;
2. `librealms_core.so`/`.dylib` next to the executable, in `../Frameworks/`
   (macOS app), in `../lib/ekron-realms/` (`.deb`), then the bare name
   (Android `jniLibs`);
3. the built-in C++ core.

`REALMS_CORE=pascal|native|auto` forces a choice (`pascal` fails if the library
is missing). `ekron-realms --engine-info` shows what was picked.

## Shared C++ (`realms/src`)

| | |
| :--- | :--- |
| `SandboxBoot` | Boot order and cvars shared by all hosts, plus the `map`, `mapinfo` and `killserver` commands |
| `MeshMap` | glTF to collision mesh |
| `MapGeometry` | Drawable triangles from BSP faces, brush windings or glTF |
| `MapWalker` | Quake 2 style movement for walking a map without the game |
| `MapLighting` | BSP lightmap atlas, light styles, `R_LightPoint` |
| `MapTextures` | `textures/*.wal` of the map's faces in one texture array, Quake 2 palette |
| `MapSky` | Skybox from worldspawn `sky`, `skyrotate`, `skyaxis`: `env/*.tga` or `.pcx` |
| `GameServer`, `Q2Pmove` | Server side of Quake 2 hosting the game module |
| `GameView`, `Md2Model` | Entity interpolation, `.md2` models, PCX skins |
| `GameEffects`, `GameSound` | Particles, temp entities, dynamic lights, beams; `.wav` decoding and spatialisation |
| `Quake2Module` | All of the above for one map, as the `realms_quake2` library |

## The Quake 2 module (`realms_quake2`)

`game/` is the Free Pascal port of the Quake 2 game DLL. `game/realms_game.lpr`
builds `librealms_game`, which exports `GetGameAPI` with game API 3
(`q2game.h`). `realms::GameServer` loads it and answers its traces through the
Engine, so the game runs on either core.

The game module is found through `GameServer::setLinkedGame`, then
`GameServerConfig::library`, then `$REALMS_GAME_LIBRARY`, then next to the
executable like the core. The desktop CMake build adds it when `fpc` is found
(`-DENABLE_GAME_MODULE=ON`). By hand:

```bash
cd game && fpc -Mdelphi -dGAMEDLL -Cg -O2 -FUunits -Fu. -Fi. -o../build/librealms_game.so realms_game.lpr
```

`realms::Quake2Module` has no renderer or audio types:

| Call | Does |
| :--- | :--- |
| `open(engine, geometry, config, why)` / `close()` | Loads lightmaps, `.wal` textures and the skybox, starts the game if the map has a worldspawn, the module exists and `sv_game` is 1 |
| `frame(userCmd(input, attack), dt)` | One sandbox frame: usercmd, `RunFrame` at 10 Hz, interpolation, effects, light styles |
| `restartGame`, `toggleNoclip`, `mirrorPlayer`, `stats`, `takeMessages`, `takeGameFailure` | Respawn, noclip, HUD values, centre prints, game errors |
| `modelPoses`, `buildScene`, `buildEffects` | Brush model poses, entity batches, effect quads and nearest dynamic lights |
| `takeSounds`, `loopSounds`, `listener` | Input for `GameSoundPlayer` |
| `lighting`, `lightmapUvs`, `lightmap`, `updateLightStyles`, `lightAt` | Lightmap atlas; model lighting from the lightmap plus all dynamic lights (`R_LightPoint`) |
| `textures`, `disableTextures` | The map's `.wal` textures; faces without one keep the generic material layers |
| `sky`, `disableSky` | The skybox; without all six images the sky gradient stays |

`mapview::Quake2View` (`sys/android/app/src/main/cpp/Quake2View.*`) converts
this to render space (metres, Y up) for the map modes:

| Platform | Map mode | Game module |
| :--- | :--- | :--- |
| Desktop | `DesktopMapMode` (OpenGL 3.3) | `librealms_game.so`/`.dylib` next to the core |
| Android | `AndroidMapMode` + `Quake2Gles` (GLES 3) | `jniLibs/<abi>/librealms_game.so` (`build_core.sh game-android-aarch64`/`-x86_64`) |
| iOS | `MetalMapMode` in `SandboxBridge.mm` | device builds link `librealms_game.a` (`build_core.sh ios-arm64`); the simulator walks maps only |

Without the game (or with `sv_game 0`) the map is walked with `MapWalker`.
glTF maps run the game as BSP maps do, with the entities their node names
spawn. Controls and map import are in [docs/CONTROLS.md](../docs/CONTROLS.md).

On Android both libraries are opened through `realms/src/FpcLibraryEnviron.h`:
FPC 3.2.2's Android startup scans backwards from `environ` for `argc`, which
faults once `setenv()` has moved `environ` to the heap.

## Boot

`realms::sandbox::boot()` on every platform:

1. registers `sensitivity`, `m_invert`, `name` and the read-only `realms_platform`;
2. mounts `pak0.pak` … `pak9.pak`;
3. runs `config/realms.cfg`, `config.cfg`, `autoexec.cfg`;
4. registers the map commands and `map_mesh_*` cvars;
5. runs `+command` arguments (desktop).

## Building the Pascal core

| Platform | How |
| :--- | :--- |
| Linux / macOS | CMake target `realms_pascal_core` when `fpc` is found (`-DENABLE_PASCAL_CORE=ON`) |
| Android | Build a cross compiler once with `realms/pascal/fpc_android_toolchain.sh <FPCSource-release_3_2_2> $HOME/fpc-android aarch64 x86_64`, then `FPC_ANDROID_PREFIX=$HOME/fpc-android ANDROID_NDK_HOME=… realms/pascal/build_core.sh android-aarch64`. Output goes to `jniLibs/arm64-v8a/` (API 30, 16 KB pages). |
| iOS | Build the RTL once on an Apple Silicon Mac with `realms/pascal/fpc_ios_toolchain.sh <FPCSource-release_3_2_2> $HOME/fpc-ios`, then `FPC_IOS_PREFIX=$HOME/fpc-ios realms/pascal/build_core.sh ios-arm64`. Xcode picks up the generated xcconfig. Link with `-Wl,-no_fixup_chains`. Simulator builds use the C++ core. |
| Any CMake project | `-DREALMS_PASCAL_STATIC_LIBRARY=/path/librealms_core.a` |
| A Pascal program | Compile the core in and pass `realms_pascal_get_api` to `realms::Engine::setLinkedPascalCore()`; `Projects/realms.lpr` does this |

## Tests

Tests that use a core run on both (`*_native` / `*_pascal` in CTest). Test
maps are generated by `bsp_builder.h`, since id maps can't ship
with the repo; `gltf_builder.h` and `md2_builder.h` do the same for glTF and
`.md2`. `maps/q2room.bsp` is the exception: compiled from `maps/q2room.map` by
id's qbsp3, qvis3 and qrad3 (`tools/build_q2_test_map.sh`). Tests that need
the game module exit 77 (skipped) without it.

| Test | Covers |
| :--- | :--- |
| `realms_bridge_test.cpp` | Cvars, commands, PAK files, collision (`realms_map_suite.h`), vis and mesh maps, glTF import, walker, Engine |
| `game_server_test.cpp` | `Q2Pmove`, the game module on a generated level, client-side entities |
| `quake2_module_test.cpp` | `Quake2Module` and `Quake2View` on a lightmapped arena, `.wal` decoding and texture layers, TGA decoding and the skybox, the game on a glTF map |
| `q2_compiled_map_test.cpp` | `q2room.bsp`: collision, areaportals, PVS, qrad lightmaps and styles, a walk with the game |
| `md2_test.cpp` | `.md2` and PCX parsing, corrupt files |
| `map_render_alignment_test.cpp` | glTF collision vs. drawn model, map import rules |
| `map_entity_transform_test.cpp` | Entity transforms in render space |

CI also runs the bridge and module tests on an Android x86_64 emulator with the
Android-built libraries, smoke-tests the APK, and runs the statically linked
core on macOS arm64 the way iOS uses it.

```bash
g++ -std=c++17 -Wall -Wextra -Werror -pedantic -Irealms/include \
  tests/realms/realms_bridge_test.cpp realms/src/{Engine,NativeCore,NativeCModel,SandboxBoot,MeshMap,MapGeometry,MapWalker}.cpp \
  -ldl -o /tmp/realms-test
/tmp/realms-test                                  # C++ core
/tmp/realms-test --pascal build/librealms_core.so # Pascal core
```

## ABI rules

- Memory is freed by the side that allocated it (`host->free_file`,
  `core->fs_free`); strings are copied into caller buffers.
- Pascal exports catch all exceptions and mask FP exceptions on entry.
- The C++ core is built without FMA contraction (`-ffp-contract=off`), so both
  cores give bit-identical traces.
- `rlm_bridge.pas` mirrors `realms_bridge.h` field for field. Change both and
  bump `REALMS_BRIDGE_VERSION`.
