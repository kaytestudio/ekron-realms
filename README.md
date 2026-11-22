# Ekron Realms

[![Desktop client (Debian)](https://github.com/kaytestudio/ekron-realms/actions/workflows/desktop-debian.yml/badge.svg)](https://github.com/kaytestudio/ekron-realms/actions/workflows/desktop-debian.yml)
[![Desktop client (macOS)](https://github.com/kaytestudio/ekron-realms/actions/workflows/desktop-macos.yml/badge.svg)](https://github.com/kaytestudio/ekron-realms/actions/workflows/desktop-macos.yml)
[![Native Android sandbox](https://github.com/kaytestudio/ekron-realms/actions/workflows/android-sandbox.yml/badge.svg)](https://github.com/kaytestudio/ekron-realms/actions/workflows/android-sandbox.yml)
[![Native iOS sandbox](https://github.com/kaytestudio/ekron-realms/actions/workflows/ios-sandbox.yml/badge.svg)](https://github.com/kaytestudio/ekron-realms/actions/workflows/ios-sandbox.yml)
[![Networking foundation](https://github.com/kaytestudio/ekron-realms/actions/workflows/network-foundation.yml/badge.svg)](https://github.com/kaytestudio/ekron-realms/actions/workflows/network-foundation.yml)

**Ekron Realms** is an open-source 3D urban sandbox and first-person exploration/building game. It features a shared native **C++17** world, rigid-body physics, and glTF 2.0 asset pipeline running natively across **Linux (`.deb`)**, **macOS (`.app`)**, **Android (`.apk`)**, and **iOS (`.ipa`)**, alongside an optional **Free Pascal** desktop client (`realms-pascal`: the Pascal Realms core running the C++ SDL2 sandbox in one program) and launcher (`realms-launcher`), an experimental **TLS online presence relay**, and the classic **Free Pascal / Lazarus** engine codebase.

---

## Key Features

- **Realms Engine (Quake 2 stack, Free Pascal + C++ bridges)**: The sandbox runs on [Realms](realms/README.md) on every platform. Realms takes the Quake 2 `qcommon` stack (console variables, command buffer and aliases, `exec`/`writeconfig` configs, the PAK filesystem), runs it as a Free Pascal core and exposes it to the C++ sandbox through one C ABI. A C++ reference core takes over when the Pascal library isn't present. Desktop, Android and iOS share the same boot sequence, cvars (`sensitivity`, `m_invert`, `name`…) and `config/realms.cfg`.
- **Shared Native C++17 Simulation Core**: Zero-duplication world state, continuous collision detection (CCD), multi-contact rigid-body solver, player movement/facade collisions, and glTF 2.0 model loading (`tinygltf` + `stb_image`) shared across Desktop, Android, and iOS.
- **3D Urban Environment**: Freely explorable wartime/abandoned city street (`abandoned_city_street_for_games.glb`) with accessible interiors, seamless procedural `sRGB` material texture arrays with mipmaps, directional lighting, exponential fog, and procedural sky gradient.
- **Building & World Persistence**: Four-tab build and equipment catalog (*Materials*, *Construction*, *Furniture*, *Tech/Vehicles* — walls, floors, windows, doors, tables, chairs, wardrobes, sofas, props, and FPV drone) with raycast holographic preview, 90° snap rotation, object removal, and automatic local persistence (`city-glb-v1.txt`).
- **Interactive Tools, Vehicles & FPV Drone**:
  - **Gravity Gun**: Grab, carry, orient, throw, and freeze/unfreeze dynamic objects in place.
  - **FPV Quadcopter & Live Controller Monitor**: Deployable FPV drone (`fpv_drone.glb`) with Mode-2 flight physics, return-to-home, FPV/chase camera switching, and an offscreen `480×270` live video feed rendered onto the handheld 3D controller (`chasing_m2_rc_underwater_drone_remote.glb`).
  - **Contextual World Interactions**: Enter and recover the **UAZ-469** (`uaz_469_1973_classic.glb`), sit on furniture, interact with doors, toggle `noclip` flight, teleport back to road/spawn, and export player coordinates as `config/spawn.json`.
- **Sound & Music (`.ogg`, CC0)**: Footsteps that follow the surface (concrete, wood, grass, carpet, snow), jump and landing, gravity-gun grab/throw/freeze, spawn and delete, doors, the UAZ engine (pitch follows speed) and its doors, FPV drone propellers (positional, pitch follows throttle), city street ambience, menu clicks and a quiet music track between silences. One shared C++ mixer (`stb_vorbis` decoding) plays through SDL2 on desktop, AAudio on Android and AVAudioEngine on iOS. Volume is `s_volume` / `s_musicvolume` (saved to `config/realms.cfg`); the console has `play <sound>`, `soundlist`, `stopsound` and `music`. All sounds are CC0 (Kenney, domasx2, FreePD, Freesound); see [`sounds/LICENSE.md`](sys/android/app/src/main/assets/sounds/LICENSE.md), rebuilt by `tools/build_sounds.py`.
- **In-Game Store (Online Play)**: An in-game shop opened with **`G`** where players spend **gold coins** earned while playing on **weapon skins** (cosmetic gravity-gun beam tints) and **unlocks** (double coins, coin bonus, golden/neon crosshair, camo HUD, start-with-flight). The economy is server-authoritative when connected to the presence relay (`DesktopOnlineClient` + `sandbox_relay.py`) and falls back to a persisted local wallet (`store.json`) offline.
- **Three UI Languages**: English, Russian (Русский), and European Portuguese (Português (Portugal)) on Desktop, Android, and iOS. Desktop follows `--lang` / `EKRON_LANG`, then the language saved from the pause menu, then the system locale; switch live with **`L`** or the pause menu's *Language* button. Android and iOS follow the system or per-app language setting (also reachable from the in-game pause menu).
- **Experimental Online Player Presence**: Optional self-hosted TLS 1.2+ relay (`server/sandbox_relay.py`) with strict certificate/hostname verification and shared-token authentication (`EKRON_JOIN_TOKEN`) that renders live remote player markers on Android, iOS, and Desktop *(player presence only; building, physics, and vehicles are not yet synchronized)*.

---

## Supported Platforms & CI Artifacts

| Platform | Source Directory | Graphics & App Stack | Package / CI Artifact | GitHub Actions Workflow |
| :--- | :--- | :--- | :--- | :--- |
| **Linux (Debian / Ubuntu)** | `sys/desktop/`, `sys/linux/` | C++17, SDL2, OpenGL 3.3 Core + optional FPC `realms-pascal` / `realms-launcher` | `ekron-realms_*.deb` (`ekron-realms-linux-deb`) | [`desktop-debian.yml`](.github/workflows/desktop-debian.yml) |
| **macOS (Apple Silicon & Intel)** | `sys/desktop/`, `sys/mac/` | C++17, SDL2, OpenGL 3.3 Core + optional FPC `realms-pascal` / `realms-launcher` | `EkronRealms.app` (`ekron-realms-macos-app`) | [`desktop-macos.yml`](.github/workflows/desktop-macos.yml) |
| **Android** | `sys/android/` | C++17, GameActivity (JNI / Kotlin), OpenGL ES 3.0 | `app-debug.apk` (`ekron-native-sandbox-debug`) | [`android-sandbox.yml`](.github/workflows/android-sandbox.yml) |
| **iOS** | `sys/ios/` | C++17, Objective-C++ / Swift (UIKit), Metal | `ekron-native-sandbox-debug.ipa` | [`ios-sandbox.yml`](.github/workflows/ios-sandbox.yml) |
| **Presence Relay (Experimental)** | `server/` | Python 3.10+ (`ssl` TLS 1.2+) | `server/sandbox_relay.py` | [`network-foundation.yml`](.github/workflows/network-foundation.yml) |

---

## Quick Start

### Clone the Repository

```bash
git clone https://github.com/kaytestudio/ekron-realms.git
cd ekron-realms
```

### Desktop — Linux (Debian / Ubuntu)

Install required dependencies (`fpc` is optional for building `realms-launcher`; `libssl-dev` is optional for experimental TLS online presence; `file` and `dpkg-dev` are used when packaging `.deb`):

```bash
sudo apt-get update
sudo apt-get install -y \
  build-essential cmake ninja-build pkg-config \
  libsdl2-dev libgl1-mesa-dev libssl-dev \
  fpc fp-units-fcl file dpkg-dev
```

Configure, build, and run:

```bash
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/usr \
  -DENABLE_FPC_CLIENT=ON
cmake --build build -j"$(nproc)"

# Run directly from the build tree
./build/ekron-realms --assets sys/android/app/src/main/assets

# Optional: build a Debian package (.deb)
(cd build && cpack -G DEB)
```

### Desktop — macOS (Apple Silicon & Intel)

Install dependencies via Homebrew (`fpc` is optional for `realms-launcher`; `openssl@3` is optional for experimental TLS online presence):

```bash
brew install cmake ninja sdl2 fpc
```

Configure, build, and launch the `.app` bundle:

```bash
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DENABLE_FPC_CLIENT=ON
cmake --build build -j"$(sysctl -n hw.ncpu)"

open build/EkronRealms.app
```

### Android (`sys/android`)

Requires **JDK 17**, **Android SDK 34** (`build-tools;34.0.0`), **NDK `26.1.10909125`**, and **CMake `3.22.1`**:

```bash
cd sys/android
./gradlew :app:assembleDebug
```

The debug APK is written to `sys/android/app/build/outputs/apk/debug/app-debug.apk`.

### iOS (`sys/ios`)

Open `sys/ios/realms/realms.xcodeproj` in **Xcode** on macOS and select the **`realms iOS`** scheme, or build an unsigned archive from the command line:

```bash
xcodebuild archive \
  -project "sys/ios/realms/realms.xcodeproj" \
  -scheme "realms iOS" \
  -configuration Debug \
  -destination "generic/platform=iOS" \
  -sdk iphoneos \
  -archivePath "/tmp/ekron-ios-sandbox.xcarchive" \
  DEVELOPMENT_TEAM="" \
  CODE_SIGN_STYLE=Manual \
  CODE_SIGN_IDENTITY="" \
  CODE_SIGNING_REQUIRED=NO \
  CODE_SIGNING_ALLOWED=NO \
  AD_HOC_CODE_SIGNING_ALLOWED=YES
```

---

## Desktop Controls & CLI Options

### Keyboard & Mouse Controls

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
| `G` / `L` | Open the in-game store / Cycle UI language (English → Русский → Português) |
| `` ` `` | Realms console (Quake 2 style: `set`, `seta`, `alias`, `exec`, `cvarlist`, `cmdlist`, `noclip`, `respawn`, `play`, `soundlist`, `s_volume`, `quit`…) |

### Command-Line Flags

```text
--version              Print client version and target architecture
--headless-smoke       Verify assets, GLB scenes, textures, physics, and save/load without opening a window
--assets <dir>         Override path to the shared Ekron assets directory
--online-host <host>   Connect to an experimental TLS presence relay (requires EKRON_JOIN_TOKEN env var)
--online-port <port>   Presence relay port (default: 38475)
--online-name <name>   Player display name (default: Player)
--lang <code>          UI language: en, ru or pt-PT (also EKRON_LANG; default: saved choice, then system locale)
--engine-info          Show which Realms core is active (Free Pascal or C++) and exit
+<command> [args]      Quake 2 style console commands, e.g. +set sensitivity 4 +exec my.cfg
--help                 Show CLI usage and controls
```

The `REALMS_CORE=pascal|native|auto` environment variable selects the Realms core. With `pascal`, startup fails if `librealms_core` is missing.

### Quake 2 Maps and the Game Module

`map <name>` in the console (or `+map <name>` on the command line) opens a Quake 2 BSP (`maps/<name>.bsp`, loose or in a `pak0.pak`…) or a `.glb` / `.gltf` scene; `killserver` returns to the sandbox. With `fpc` installed the build also produces `librealms_game.so` / `.dylib`, the Free Pascal port of the Quake 2 game DLL (`game/`). The C++ sandbox hosts it through `realms::GameServer`, a Quake 2 server on the Realms Engine, so a BSP map plays like Quake 2: the player spawns at `info_player_start`, items can be picked up, doors, lifts and triggers work, and monsters and weapons run the original game code, on the Pascal or the C++ core. Items, monsters, projectiles and the view weapon are drawn from the `.md2` models and skins in your `pak0.pak`, interpolated between game frames like the Quake 2 client. Without id's data each one is shown as a coloured box of its size. `--engine-info` shows the module (`Game: … (Quake 2 game API 3)`).

| Input (in a map) | Action |
| :--- | :--- |
| `W A S D` / `Space` / `Ctrl` | Move / Jump / Crouch |
| `Left Click` | Fire the current weapon (with the mouse captured) |
| `N` / `R` | Noclip / Restart the map (respawn without the game) |
| `` ` `` | Console: Quake 2 client commands such as `give all`, `give shotgun`, `use blaster`, `god`, `notarget`, `kill`, `weapnext`, `cmd <command>`, server commands with `sv <command>`, and `killserver` |

`sv_game 0` turns the game off (then `map` only walks the map), and `cl_gun 0` hides the view weapon. `REALMS_GAME_LIBRARY=/path/to/librealms_game.so` picks another module build. Details: [realms/README.md](realms/README.md#quake-2-game-module-gameserver).

### Free Pascal Client (`realms-pascal`)

With `fpc` installed and `-DENABLE_FPC_CLIENT=ON`, the build also produces `realms-pascal` (`Projects/realms.lpr`). It is a Free Pascal program with the Pascal Realms core compiled in. It runs the C++ desktop sandbox (window, input, OpenGL, audio, world) from `libekron_sandbox`, so SDL2 is used from C++ and no Pascal SDL units are needed. It takes the same flags as `ekron-realms`. `--engine-info` reports `Core: pascal-quake2 (Free Pascal)` and `Origin: linked`. It is installed as `/usr/bin/realms-pascal` by the `.deb` and as `EkronRealms.app/Contents/MacOS/realms-pascal` on macOS.

You can also build it from the Lazarus project `Projects/realms.lpi` (`lazbuild Projects/realms.lpi`), with `make pascal`, or with `fpc @realms-fpc.cfg Projects/realms.lpr`. The top-level `Makefile` (`make`, `make test`, `make run`, `make package`) and the `Dockerfile` / `docker compose up` / `Jenkinsfile` wrap the same CMake build. See [BUILD.md](BUILD.md#free-pascal-client-realms-pascal).

---

## Testing & Verification

```bash
# Run desktop CTest suite (HUD layout, shop, localization, Realms bridge conformance
# for the C++ and Free Pascal cores, console, sound decoding/mixing, --engine-info,
# --version, --headless-smoke, the realms-pascal program on its linked core, and the
# Quake 2 game module hosted by realms::GameServer on both cores)
ctest --test-dir build --output-on-failure -V

# Validate Android and iOS translations (English, Russian, Portuguese (Portugal))
python3 tools/validate_i18n.py

# Validate Android GLSL ES 3.0 and iOS Metal shaders
python3 tools/validate_android_shaders.py
python3 tools/validate_ios_shaders.py

# Run experimental TLS presence relay protocol tests
python3 -m unittest discover -s tests/network -v
```

Standalone native C++17 unit tests covering rigid-body physics, swept floor/CCD collisions, indoor/raycast placement, glTF loading, FPV drone simulation, sound (every shipped `.ogg` decodes and is credited; mixer and game-event director), HUD scaling, and online presence live under [`tests/native/`](tests/native) and run automatically in CI.

---

## Repository Structure & Documentation

```text
ekron-realms/
├── CMakeLists.txt              # Desktop CMake build, CTest smoke suite, and CPack (.deb / .app) packaging
├── cmake/                      # CMake find-modules (SDL2, SDL3, FPC, Lazarus, KayteIDE, KCC)
├── realms/                     # Realms Engine: C ABI + C++ facade (include/, src/) and Free Pascal Quake 2 core (pascal/)
├── game/                       # Quake 2 game module (Free Pascal gamex86 port) → librealms_game, hosted by realms::GameServer
├── Resources/                  # macOS Info.plist and Linux .desktop launcher entry
├── sys/
│   ├── android/                # Native Android sandbox (GameActivity + OpenGL ES 3.0 + shared C++17 core & assets)
│   ├── ios/                    # Native iOS sandbox (UIKit + Metal Xcode project sharing C++17 core & assets)
│   ├── desktop/                # Native Linux & macOS desktop client (SDL2 + OpenGL 3.3 Core)
│   ├── linux/ & mac/           # Platform units for the Free Pascal launcher & legacy engine
│   └── ...                     # Additional legacy platform ports
├── server/                     # Experimental Python TLS 1.2+ online presence relay
├── Projects/                   # Free Pascal client (realms.lpr → realms-pascal), launcher (realms_launcher.lpr), legacy Q2 graph (realms_q2legacy.lpr), Lazarus / Delphi projects
├── tests/                      # C++17 simulation/physics tests (tests/native), Realms bridge tests (tests/realms) & TLS relay tests (tests/network)
├── tools/                      # Texture generator, catalog icon renderer, sound pack builder, shader validators; SDL2/SDL3-for-Pascal submodules; tools/kcc (-DENABLE_KCC=ON)
└── docs/                       # Sandbox design notes and cross-platform HUD verification guide
```

### Further Reading

- [`realms/README.md`](realms/README.md) — Realms Engine architecture, Pascal ⇄ C++ bridge ABI, backend selection, and building the Pascal core
- [`sys/desktop/README.md`](sys/desktop/README.md) — Native Linux & macOS desktop client guide (Russian)
- [`sys/android/README.md`](sys/android/README.md) — Native Android sandbox architecture & build notes (Russian)
- [`sys/ios/README.md`](sys/ios/README.md) — Native iOS Metal sandbox & unsigned IPA instructions (Russian)
- [`server/README.md`](server/README.md) — Experimental TLS online presence relay setup, security notes, and protocol v1
- [`docs/HUD_TESTING.md`](docs/HUD_TESTING.md) — Cross-platform HUD & input verification checklist
- [`docs/SANDBOX_EN.md`](docs/SANDBOX_EN.md) · [`docs/SANDBOX_PT_PT.md`](docs/SANDBOX_PT_PT.md) · [`docs/SANDBOX_RU.md`](docs/SANDBOX_RU.md) — Urban sandbox design direction
- [`BUILD.md`](BUILD.md) — Extended CMake, Free Pascal, and Lazarus build reference

---

## Contributing

Contributions, bug reports, and feature discussions are welcome!

1. Fork the repository and create a feature branch from `main`.
2. Build and run the relevant test suites (`ctest`, `tests/native`, or `tests/network`) for your changes.
3. Open a pull request against `main` with a clear summary of what changed and how it was verified.

Please review [`CONTRIBUTING.md`](CONTRIBUTING.md) and our [`CODE_OF_CONDUCT.md`](CODE_OF_CONDUCT.md) before submitting contributions.

---

## License

Ekron Realms is free and open-source software licensed under the **GNU Affero General Public License v3.0 (`AGPL-3.0`)** (with individual vendored third-party libraries such as `stb_image`, `stb_vorbis` and `tinygltf` under their respective permissive licenses). See [`LICENSE`](LICENSE) for the full license text.
