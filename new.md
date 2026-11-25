# Ekron Realms: overview

Ekron Realms is an open-source first-person city sandbox by Kayte Studio. You walk an abandoned street, build with walls, furniture and props, carry things with a gravity gun, drive a UAZ-469 and fly an FPV drone. It can also load and play Quake 2 maps.

The README has the short version; this page goes through the project in more detail.

## Two modes

- **The sandbox**: the city street, the build tools and vehicles, sounds and music, a cosmetics shop paid with in-game coins, and an experimental presence relay (`server/`) that shows other players.
- **Map mode**: `map <name>` opens a Quake 2 `.bsp` map or a glTF scene (`.glb` / `.gltf`). With the Free Pascal port of the Quake 2 game code it plays as Quake 2 does: spawn points, items, doors, lifts, triggers, monsters and weapons, with the game's status bar, help computer and inventory, and Quake 2's `save` / `load`. Your own Quake 2 data (`baseq2/pak0.pak`) supplies textures and sounds; models are Quake 3 `.md3` files. The apps include one sample glTF map, `courtyard`, which needs no Quake 2 files. See [docs/CONTROLS.md](docs/CONTROLS.md#quake-2-maps).

The UI is available in English, Russian, European Portuguese and Spanish (Spain).

## Platforms

All platforms run the same C++17 game code.

| Platform | Package | Graphics |
| :--- | :--- | :--- |
| Linux (amd64) | `.deb` | SDL2, OpenGL 3.3 |
| macOS | `.app` (ad-hoc signed, not notarised) | SDL2, OpenGL 3.3 |
| Windows 10 and 11 (x64) | portable `.zip` | SDL2, OpenGL 3.3 |
| Windows 11 on ARM (arm64) | portable `.zip` (C++ core, no Quake 2 game module yet) | SDL2, OpenGL 3.3 |
| Anbernic RG40XXH and other aarch64 handhelds | PortMaster port `.zip` | SDL2, OpenGL ES 3.0, gamepad |
| Android 11+ (arm64-v8a, x86_64) | debug `.apk` | OpenGL ES 3.0 |
| iOS 16+ | unsigned `.ipa` | Metal |

The release workflow attaches all seven packages to a GitHub release; [BUILD.md](BUILD.md#packaging) explains how they are made.

## The engine: Realms

Under the game is [Realms](realms/README.md): Quake 2's `qcommon` layer (file system and paks, collision model, console, commands and cvars) as a Free Pascal library behind a C ABI. When Free Pascal is not available, a C++ version of the same layer is used. `game/` is the Free Pascal port of the Quake 2 game DLL, built as `librealms_game` and run by map mode.

## Free Pascal and Lazarus

Free Pascal 3.2 builds the Realms core, the game module and `realms-pascal`, a Free Pascal program that runs the sandbox, plus `realms-launcher` (these two are Linux and macOS only). CMake runs `fpc` itself, so Lazarus is not needed to build the game. The Lazarus projects (`Projects/realms.lpi`, `game/realms_game.lpi`) are still kept for anyone who works in the IDE.

## Getting started

```bash
git clone https://github.com/kaytestudio/ekron-realms.git
cd ekron-realms
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/ekron-realms --assets sys/android/app/src/main/assets
```

Required tools for each platform, the Android and iOS builds and the tests are covered in [BUILD.md](BUILD.md). Controls and command-line options are in [docs/CONTROLS.md](docs/CONTROLS.md).

## Contributing and license

[CONTRIBUTING.md](CONTRIBUTING.md) explains how to report bugs and send changes, and [CONTRIBUTORS.md](CONTRIBUTORS.md) lists the people and projects behind the game. Ekron Realms is licensed under the [GNU Affero General Public License v3.0](LICENSE). The Quake 2 source it builds on is GPL, and the bundled models and sounds keep their own licenses.

## Links

- [GitHub repository](https://github.com/kaytestudio/ekron-realms)
- [Free Pascal](https://www.freepascal.org/)
- [Lazarus IDE](https://www.lazarus-ide.org/)
