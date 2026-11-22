# Ekron Realms

A first-person city sandbox: walk an abandoned street, build with walls, furniture and props, carry things with a gravity gun, drive a UAZ-469 and fly an FPV drone. The game runs from the same C++17 code on Linux, macOS, Android and iOS.

Under it is [Realms](realms/README.md): Quake 2's `qcommon` layer as a Free Pascal library behind a C ABI (with a C++ fallback), which also loads Quake 2 maps and runs the Pascal port of the Quake 2 game code (`game/`).

Also:

- sounds and music: CC0 `.ogg`, one mixer for SDL2, AAudio and AVAudioEngine ([credits](sys/android/app/src/main/assets/sounds/LICENSE.md))
- English, Russian and European Portuguese UI
- `realms-pascal`, a Free Pascal program running the sandbox, and `realms-launcher`
- an experimental presence relay (`server/`) that shows other players
- a cosmetics shop paid with in-game coins

## Platforms

| Platform | Code | Stack | CI |
| :--- | :--- | :--- | :--- |
| Linux | `sys/desktop/` | SDL2, OpenGL 3.3, `.deb` | [desktop-debian](.github/workflows/desktop-debian.yml) |
| macOS | `sys/desktop/` | SDL2, OpenGL 3.3, `.app` | [desktop-macos](.github/workflows/desktop-macos.yml) |
| Android | `sys/android/` | GameActivity, OpenGL ES 3.0 | [android-sandbox](.github/workflows/android-sandbox.yml) |
| iOS | `sys/ios/` | UIKit, Metal | [ios-sandbox](.github/workflows/ios-sandbox.yml) |
| Relay | `server/` | Python 3.10+ | [network-foundation](.github/workflows/network-foundation.yml) |

## Building

Desktop (Linux shown; on macOS `brew install cmake ninja sdl2 fpc`):

```bash
sudo apt-get install -y build-essential cmake ninja-build libsdl2-dev libgl1-mesa-dev libssl-dev fpc
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DENABLE_FPC_CLIENT=ON
cmake --build build
./build/ekron-realms --assets sys/android/app/src/main/assets
```

Android, iOS, packaging and the Free Pascal options are in [BUILD.md](BUILD.md). Controls and command-line flags are in [docs/CONTROLS.md](docs/CONTROLS.md).

## Tests

```bash
ctest --test-dir build --output-on-failure
python3 tools/validate_i18n.py
python3 tools/validate_android_shaders.py && python3 tools/validate_ios_shaders.py
python3 -m unittest discover -s tests/network
```

CI also runs the Realms tests on an Android emulator and with the static iOS libraries on macOS.

## Layout

- `realms/`: Realms engine (C ABI, C++ side, Pascal core)
- `game/`: Quake 2 game module (Free Pascal)
- `sys/desktop`, `sys/android`, `sys/ios`: platform clients
- `server/`: presence relay
- `Projects/`: Free Pascal programs and Lazarus projects
- `tests/`, `tools/`, `docs/`

The platform READMEs under `sys/` are in Russian.

## License

AGPL-3.0, see [LICENSE](LICENSE). Vendored `stb_image`, `stb_vorbis` and `tinygltf` keep their own licenses. See [CONTRIBUTING.md](CONTRIBUTING.md).
