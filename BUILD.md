# Building Realms

This document describes how to build the Realms project using CMake on Windows, macOS, and Linux.

## Table of Contents

- [Prerequisites](#prerequisites)
- [Quick Start](#quick-start)
- [Platform-Specific Instructions](#platform-specific-instructions)
  - [macOS](#macos)
  - [Windows 11](#windows-11)
  - [Linux](#linux)
- [Build Types](#build-types)
- [Build Targets](#build-targets)
- [Advanced Configuration](#advanced-configuration)
- [Android and iOS](#android-and-ios)
- [Troubleshooting](#troubleshooting)

---

## Prerequisites

### Required

- **CMake** 3.16 or higher
- **Free Pascal Compiler (FPC)** 3.2.0 or higher
- **Lazarus IDE** 2.0.0 or higher (optional, but recommended)

### Installation

#### macOS

```bash
# Using Homebrew
brew install cmake
brew install fpc
brew install lazarus

# Or using MacPorts
sudo port install cmake
sudo port install fpc
sudo port install lazarus
```

#### Windows 11

The desktop client builds with MinGW-w64 from [MSYS2](https://www.msys2.org/) (the toolchain of the release package) or with Visual Studio 2022. See [Windows 11](#windows-11) below.

1. A C++ toolchain: MSYS2 (in a **UCRT64** shell: `pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja`), or Visual Studio 2022 with *Desktop development with C++* (it includes CMake)
2. SDL2: the official development package from the [SDL 2 releases](https://github.com/libsdl-org/SDL/releases): `SDL2-devel-2.x.y-mingw.tar.gz` for MinGW, `SDL2-devel-2.x.y-VC.zip` for Visual Studio
3. Optional, for the Free Pascal Realms core and the Quake 2 game module: [Lazarus](https://www.lazarus-ide.org/index.php?page=downloads) 64-bit (includes FPC; default location `C:\lazarus`), or FPC standalone from [FreePascal.org](https://www.freepascal.org/download.html)

#### Linux (Debian/Ubuntu)

```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake ninja-build pkg-config \
  libsdl2-dev libgl1-mesa-dev libssl-dev fpc fp-units-fcl file dpkg-dev \
  libavformat-dev libavcodec-dev libswscale-dev libswresample-dev
sudo apt-get install -y lazarus   # optional, for lazbuild
```

`fpc` is only needed for the Pascal core, the game module and `realms-pascal`; `libssl-dev` only for the online presence client; the FFmpeg packages only for the intro video (`intro.mp4` at startup; without them the game starts straight away). macOS plays the intro through AVFoundation and needs nothing extra.

#### Linux (Fedora/RHEL)

```bash
sudo dnf install cmake gcc
sudo dnf install fpc
sudo dnf install lazarus
```

#### Linux (Arch)

```bash
sudo pacman -S cmake
sudo pacman -S fpc
sudo pacman -S lazarus
```

---

## Quick Start

```bash
# 1. Clone the repository (or navigate to project directory)
cd /path/to/realms

# 2. Create build directory
mkdir build
cd build

# 3. Configure the project
cmake -DCMAKE_BUILD_TYPE=Release ..

# 4. Build
cmake --build .

# 5. Run
./ekron-realms              # Linux/macOS
.\ekron-realms.exe          # Windows (Release\ekron-realms.exe with Visual Studio)
```

---

## Platform-Specific Instructions

### macOS

#### Intel Macs

```bash
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
cmake --build .
./realms
```

#### Apple Silicon (M1/M2/M3/M4)

```bash
mkdir build && cd build

# CMake will automatically detect ARM64 architecture
cmake -DCMAKE_BUILD_TYPE=Release ..
cmake --build .
./realms
```

**Note**: If you have both Intel and Apple Silicon FPC installed, CMake will use the native architecture by default.

#### Creating macOS App Bundle (Optional)

```bash
# Build and package
cmake --build .
cpack -G DragNDrop

# This creates a .dmg file in the build directory
```

### Windows 11

The native C++ client (`ekron-realms.exe`, SDL2 + OpenGL) runs on 64-bit Windows 10 and 11. CI (`.github/workflows/desktop-windows.yml`) builds it on Windows Server 2025, the server edition of Windows 11 24H2, with both toolchains below, and makes the release ZIP with MinGW.

#### MinGW-w64 (MSYS2 UCRT64)

In an MSYS2 **UCRT64** shell, with the SDL2 MinGW package unpacked to `C:/SDL2` and, optionally, Lazarus in `C:\lazarus`:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DSDL2_DIR=C:/SDL2/cmake \
  -DFPC_EXECUTABLE=C:/lazarus/fpc/3.2.2/bin/x86_64-win64/fpc.exe   # optional
cmake --build build
ctest --test-dir build --output-on-failure
./build/ekron-realms.exe
```

`ekron-realms.exe` links the C++ runtime statically; `SDL2.dll` is copied next to it, and FPC (when found) adds `realms_core.dll` and `realms_game.dll`, so the build folder runs as is.

#### Visual Studio 2022

From a Developer PowerShell, with the SDL2 VC package unpacked to `C:\SDL2`:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DSDL2_DIR=C:/SDL2/cmake
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
.\build\Release\ekron-realms.exe
```

The Visual C++ runtime is linked statically (`/MT`), so the exe does not need the VC++ redistributable. The Free Pascal DLLs can be built by the same configure step when `FPC_EXECUTABLE` is set (or `fpc` is on `PATH`).

#### Windows notes

- **Package**: `cpack -G ZIP` (the default generator on Windows) makes `ekron-realms-<version>-windows-x64.zip`, a portable folder with `ekron-realms.exe`, `SDL2.dll`, the Free Pascal DLLs and `assets\`. Unzip it anywhere; there is no installer.
- **User data** (saves, settings): `%APPDATA%\KayteStudio\EkronRealms\`. Quake 2's `baseq2` folder (with `pak0.pak`) is found there or in `assets\`, or pass `--quake2 <dir>` / `EKRON_QUAKE2_DIR`; `--engine-info` shows what is found.
- **Intro video**: decoded by Media Foundation, part of Windows. Windows N editions need Microsoft's *Media Feature Pack* (Settings > Apps > Optional features); without it the game starts without the intro.
- **Console output**: the exe is a GUI program (no console window from Explorer). Started from Command Prompt, PowerShell or Windows Terminal, `--help`, `--version`, `--engine-info` and the log print to that window.
- **Not on Windows**: the Free Pascal launcher programs (`realms-launcher`, `realms-pascal`, which use the Unix sys layer) and the experimental online presence (POSIX sockets + OpenSSL). The legacy `sys/win32` and `sys/win64` Pascal sources are not built.
- **SmartScreen**: the release exe is not code-signed, so Windows may warn on first start: **More info**, then **Run anyway**.

### Linux

#### Standard Build

```bash
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
cmake --build . -j$(nproc)  # Use all CPU cores
./realms
```

#### Install System-Wide

```bash
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr/local ..
cmake --build .
sudo cmake --install .

# Now you can run from anywhere
realms
```

#### Creating Linux Packages

```bash
# DEB package (Debian/Ubuntu)
cmake --build .
cpack -G DEB

# RPM package (Fedora/RHEL)
cpack -G RPM

# TGZ archive
cpack -G TGZ
```

---

## Build Types

The project supports three build types:

### Release (Recommended for end users)

```bash
cmake -DCMAKE_BUILD_TYPE=Release ..
```

- Maximum optimization (`-O3`)
- Stripped symbols
- Smallest binary size
- Best performance

### Debug (Recommended for developers)

```bash
cmake -DCMAKE_BUILD_TYPE=Debug ..
```

- Debug symbols (`-g`)
- Runtime checks (range, overflow, I/O)
- Heap trace enabled
- Assertions enabled
- Easier debugging with gdb/lldb

### Default (No optimization flags specified)

```bash
cmake ..
```

- Basic optimization (`-O2`)
- Debug info included
- Balanced build

---

## Build Targets

The project provides several build targets:

### Main Targets

| Target | Description |
|--------|-------------|
| `realms` | Main application (Release mode) |
| `realms_debug` | Debug version with runtime checks |
| `realms_verbose` | Verbose build output (Lazarus only) |

### Build Specific Target

```bash
# Build only the main release version
cmake --build . --target realms

# Build only the debug version
cmake --build . --target realms_debug

# Build all targets
cmake --build .
```

### Optional Targets

| Target | Description |
|--------|-------------|
| `console_app` | Example console application (if src/main.pas exists) |
| `multi_unit_app` | Example multi-unit app (if src/program.pas exists) |

### Utility Targets

```bash
# Clean build artifacts
cmake --build . --target clean

# Deep clean (removes all Pascal temp files)
cmake --build . --target distclean
```

---

## Advanced Configuration

### Specifying Lazarus Location

If CMake can't find Lazarus automatically:

```bash
cmake -DLAZARUS_ROOT_DIR=/path/to/lazarus ..
```

### Specifying FPC Location

If CMake can't find FPC automatically:

```bash
cmake -DFPC_EXECUTABLE=/path/to/fpc ..
```

### Free Pascal Client (`realms-pascal`)

`Projects/realms.lpr` builds `realms-pascal`, a Free Pascal program with the Pascal Realms core (`realms/pascal`) compiled in. It runs the C++ desktop sandbox, built a second time as a shared library (`libekron_sandbox.so` / `.dylib`, entry point `ekron_sandbox_run()` in `sys/desktop/ekron_sandbox.h`). Window, input, OpenGL and audio therefore use SDL2 from C++. The sandbox gets the core's `realms_pascal_get_api` table directly, origin `linked`, instead of loading `librealms_core`. CMake builds it with `-DENABLE_FPC_CLIENT=ON` when `fpc` is found (target `realms_pascal_client`):

| | Program | Sandbox library |
| :--- | :--- | :--- |
| Linux build tree | `build/realms-pascal` | `build/libekron_sandbox.so` (RUNPATH `$ORIGIN`) |
| `.deb` | `/usr/bin/realms-pascal` | `/usr/lib/ekron-realms/libekron_sandbox.so` |
| macOS | `EkronRealms.app/Contents/MacOS/realms-pascal` | `EkronRealms.app/Contents/Frameworks/libekron_sandbox.dylib` |

```bash
cmake -S . -B build -DENABLE_FPC_CLIENT=ON && cmake --build build
build/realms-pascal --engine-info     # Core: pascal-quake2 (Free Pascal) / Origin: linked
build/realms-pascal                   # the game
```

The other ways to build it all produce the same `build/realms-pascal`. Each one first builds `libekron_sandbox` with CMake in `<repo>/build`; `Projects/build_sandbox.sh` does that and configures the build directory if needed:

| Entry point | Command |
| :--- | :--- |
| Lazarus | Open `Projects/realms.lpi` (or the root `realms.lpi`) and press Build, or run `lazbuild Projects/realms.lpi`. Its "Execute before" step runs `Projects/build_sandbox.sh`, which also builds the Quake 2 game module (`build/librealms_game.so`). `game/realms_game.lpi` builds only the game module. |
| Makefile | `make pascal` (or `make lazarus` for lazbuild), `make game` for the game module. `make`, `make test`, `make run`, `make run-pascal`, `make package` and `make install` wrap the CMake build. |
| Plain fpc | `Projects/build_sandbox.sh && fpc @realms-fpc.cfg Projects/realms.lpr` |
| Docker / Jenkins | `docker build -t ekron-builder .` runs the CMake build, CTest and CPack on Ubuntu 24.04. `docker compose up` (or the `Jenkinsfile`) copies the `.deb` and the binaries to `./output`. |

The CI workflow `build-entry-points.yml` runs all of them. The old Quake 2 client is kept as `Projects/realms_q2legacy.lpr` / `Projects/realms_q2legacy.lpi`, which used to be `realms.lpr` / `realms.lpi`. It does not build: six `sys/linux` units it names don't exist, and the engine units don't compile together yet.

### Pascal SDL2 Units and Other Submodules

The legacy Free Pascal sys layer (`sys/mac/sys_mac.pas`, `Projects/realms_q2legacy.lpi`) uses the SDL2 units in `tools/SDL2-for-Pascal`. That folder, `tools/SDL3-for-Pascal` and `ide` (KayteIDE) are git submodules. The desktop C++ client, `realms-pascal`, the launcher and the Realms core don't need them. Fetch the SDL2 units with:

```bash
git submodule update --init tools/SDL2-for-Pascal
# or everything: git submodule update --init
```

Units listed with `in '...'` resolve from the directory fpc runs in. `Projects/realms_q2legacy.lpr` and `realms_q2legacy.lpi` compile from `Projects/`. `tests/pascal/sdl2_sys_smoke.lpr` uses SDL2 the same way `sys_mac.pas` does, and the desktop CI builds and runs it:

```bash
cd Projects
fpc -Mobjfpc -FU/tmp/sdl2u -FE/tmp/sdl2u ../tests/pascal/sdl2_sys_smoke.lpr   # macOS: add -Fl$(brew --prefix sdl2)/lib
/tmp/sdl2u/sdl2_sys_smoke
```

### KCC (Kayte C Compiler)

`-DENABLE_KCC=ON` builds [KCC](https://github.com/kaytestudio/kcc) as part of the build. CMake uses `tools/kcc` if you already cloned it there:

```bash
git clone https://github.com/kaytestudio/kcc.git tools/kcc
```

Otherwise it clones it there for you. Only the `kcc` compiler is built, in `<build>/kcc/bin/kcc`, with KCC's GCC SH-2 cross-compiler and Saturn tools switched off. `-DENABLE_EXTERNAL_TOOLS=ON` also fetches KayteIDE.

```bash
cmake -S . -B build -DENABLE_KCC=ON
cmake --build build --target kcc
build/kcc/bin/kcc --version
```

`KCC_REPO`, `KCC_GIT_TAG` (default `main`) and `KCC_SOURCE_DIR` override the source. The `clean-kcc` target deletes the checkout and the build.

### Custom Install Prefix

```bash
# Install to custom directory
cmake -DCMAKE_INSTALL_PREFIX=/opt/realms ..
cmake --build .
cmake --install .
```

### Verbose Build Output

```bash
# See actual compiler commands
cmake --build . --verbose

# Or with make
make VERBOSE=1
```

### Parallel Building

```bash
# Linux/macOS - use all CPU cores
cmake --build . -j$(nproc)

# Windows - use 8 cores
cmake --build . -j 8
```

### Cross-Compilation

The build system automatically detects the target platform, but you can manually specify:

```bash
# For Windows target (from Linux with cross-compiler)
cmake -DCMAKE_SYSTEM_NAME=Windows ..

# For specific architectures
cmake -DFPC_TARGET_CPU=x86_64 ..
cmake -DFPC_TARGET_CPU=aarch64 ..
```

---

## Android and iOS

Android needs JDK 17, Android SDK 34 (`build-tools;34.0.0`), NDK `26.1.10909125` and CMake `3.22.1`:

```bash
cd sys/android && ./gradlew :app:assembleDebug
# -> sys/android/app/build/outputs/apk/debug/app-debug.apk
```

iOS: open `sys/ios/realms/realms.xcodeproj` in Xcode and build the `realms iOS` scheme. An unsigned archive from the command line:

```bash
xcodebuild archive -project sys/ios/realms/realms.xcodeproj -scheme "realms iOS" \
  -configuration Debug -destination generic/platform=iOS -sdk iphoneos \
  -archivePath /tmp/ekron-ios-sandbox.xcarchive DEVELOPMENT_TEAM= CODE_SIGN_STYLE=Manual \
  CODE_SIGN_IDENTITY= CODE_SIGNING_REQUIRED=NO CODE_SIGNING_ALLOWED=NO AD_HOC_CODE_SIGNING_ALLOWED=YES
```

---

## Realms Engine (Pascal core + C++ bridge)

The sandbox runs on the Realms Engine (see [`realms/README.md`](realms/README.md)). It is built as follows:

- `realms_engine` is a static C++ library and is always built. It contains the bridge, the `realms::Engine` facade and the C++ reference core.
- `realms_pascal_core` is built when `fpc` is found and `-DENABLE_PASCAL_CORE=ON` (the default). It compiles `realms/pascal/realms_core.lpr` into `librealms_core.so`/`.dylib`:
  - in a build-tree run, next to `ekron-realms`;
  - in the macOS `.app`, in `EkronRealms.app/Contents/Frameworks/`;
  - in the `.deb`, under `/usr/lib/ekron-realms/`.

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DENABLE_PASCAL_CORE=ON
cmake --build build
./build/ekron-realms --engine-info                    # which core is active
REALMS_CORE=pascal ./build/ekron-realms --headless-smoke
ctest --test-dir build -R realms --output-on-failure  # bridge conformance (C++ and Pascal)
```

For Android `jniLibs` and the iOS static archive, use `realms/pascal/build_core.sh android-aarch64|android-armv7|android-x86_64|ios-arm64`. Without a Pascal core, every platform falls back to the C++ reference core. `build_core.sh game-android-aarch64|game-android-armv7|game-android-x86_64` puts the Quake 2 game module (`librealms_game.so`) into the same `jniLibs/<abi>` folders, and `ios-arm64` (like `macos-arm64-static`) also writes `librealms_game.a`, which the generated xcconfig links with `REALMS_GAME_STATIC=1`.

- `realms_game_module` is built when `fpc` is found and `-DENABLE_GAME_MODULE=ON` (the default). It compiles the Quake 2 game module, `game/realms_game.lpr` (the Free Pascal gamex86 port), into `librealms_game.so`/`.dylib`, installed next to `librealms_core` in the same three places. `realms::GameServer` (part of `realms_quake2`, the Quake 2 module that the desktop, Android and iOS sandboxes link) loads it when a BSP map is opened, so maps run the Quake 2 game (items, doors, triggers, monsters, weapons) on whichever core is active. `make game` and `lazbuild game/realms_game.lpi` build the same library; `REALMS_GAME_LIBRARY=/path/librealms_game.so` overrides the search; the `sv_game 0` cvar turns the game off (map mode then only walks the map).

```bash
cmake --build build --target realms_game_module
./build/ekron-realms --engine-info                    # Game: …/librealms_game.so (Quake 2 game API 3)
./build/ekron-realms --quake2 ~/quake2 +map base1    # needs maps/base1.bsp, e.g. from ~/quake2/baseq2/pak0.pak
./build/ekron-realms --engine-info --quake2 ~/quake2  # Quake 2: …/baseq2 (pak0.pak, …)
ctest --test-dir build -R game --output-on-failure    # realms_game_server_native / _pascal, realms_game_effects
ctest --test-dir build -R map_lighting --output-on-failure   # realms_map_lighting (BSP lightmaps)
ctest --test-dir build -R quake2_module --output-on-failure  # realms_quake2_module_native / _pascal (Quake2Module + Quake2View)
ctest --test-dir build -R q2_compiled --output-on-failure    # realms_q2_compiled_map_native / _pascal (a qbsp3/qrad3 map)
tools/build_q2_test_map.sh                                   # rebuilds tests/realms/maps/q2room.bsp (curl, make, cc)
ctest --test-dir build -R sample_map --output-on-failure     # realms_sample_map_native / _pascal (the shipped courtyard.glb)
python3 tools/build_sample_map.py                            # rebuilds assets maps/courtyard.glb and env/courtyard_*.tga
```

`realms/src/GameEffectTables.inc` is generated and committed. It holds the byte directions and the monster muzzle flashes used by the client effects. If `game/m_flash.pas` or `engine/client/cl_fx.pas` changes, regenerate it with `python3 tools/gen_game_effect_tables.py` (plain Python 3, no packages needed).

---

## Testing

The project includes CTest support:

```bash
# Run all tests
ctest

# Run tests with verbose output
ctest --output-on-failure

# Run specific test
ctest -R test_realms_version

# Run tests in parallel
ctest -j$(nproc)
```

---

## Packaging

Create distribution packages:

```bash
# Configure and build first
cmake -DCMAKE_BUILD_TYPE=Release ..
cmake --build .

# Create package
cpack

# Or specify generator
cpack -G ZIP        # ZIP archive (the default on Windows: portable folder)
cpack -G DragNDrop  # macOS DMG
cpack -G DEB        # Debian package
cpack -G RPM        # RPM package
```

### Package Components

You can install specific components:

```bash
# Install only the application
cmake --install . --component applications

# Install with debug binaries
cmake --install . --component debug

# Install development files
cmake --install . --component development

# Install documentation
cmake --install . --component documentation
```

### Releases

Pushing a tag that starts with `v` (for example `git tag v1.0.0 && git push origin v1.0.0`, or `gh release create v1.0.0`) runs `.github/workflows/release.yml`. It builds the packages with the regular CI workflows and attaches them to the tag's GitHub release:

| File | From |
| :--- | :--- |
| `ekron-realms-<version>-linux-amd64.deb` | `desktop-debian.yml` |
| `ekron-realms-<version>-macos-unsigned.app.zip` | `desktop-macos.yml` (ad-hoc signed, not notarised) |
| `ekron-realms-<version>-android-debug.apk` | `android-sandbox.yml` (debug key) |
| `ekron-realms-<version>-ios-debug-unsigned.ipa` | `ios-sandbox.yml` (unsigned) |
| `ekron-realms-<version>-windows-x64.zip` | `desktop-windows.yml` (MinGW-w64; exe not code-signed) |
| `SHA256SUMS.txt` | checksums of the five |

A release that already exists keeps its notes and gets the files (re-running replaces them). Otherwise the workflow creates the release with install notes and generated release notes, as a pre-release when the version has a suffix (`v1.1.0-rc1`). Tag pushes don't trigger the five build workflows on their own, so each tag is built once. The workflow warns when the tag differs from `project(EkronRealms VERSION ...)` in `CMakeLists.txt`. That project version is also the version inside every package: the desktop builds, the Android `versionName` (with `versionCode` = x·10000 + y·100 + z, read by `sys/android/app/build.gradle.kts`) and the iOS `MARKETING_VERSION` (passed by `ios-sandbox.yml`). The Android and iOS workflows check it in the built app, so bump it in `CMakeLists.txt` only.

---

## Troubleshooting

### CMake Can't Find FPC

**Problem**: `Could not find FPC`

**Solution**:
```bash
# Find your FPC installation
which fpc          # Linux/macOS
where fpc          # Windows

# Specify it explicitly
cmake -DFPC_EXECUTABLE=/usr/local/bin/fpc ..
```

### CMake Can't Find Lazarus

**Problem**: `Could not find Lazarus IDE`

**Solution**:
```bash
# Lazarus is optional - the project will build with FPC alone
# But if you want to use Lazarus:
cmake -DLAZARUS_ROOT_DIR=/path/to/lazarus ..

# Common Lazarus locations:
# macOS: /Applications/Lazarus.app/Contents/MacOS
# Windows: C:/lazarus
# Linux: /usr/lib/lazarus or /usr/share/lazarus
```

### Build Fails with "File not found"

**Problem**: Missing project files

**Solution**: Ensure you have the correct project structure:
```
Realms/
├── CMakeLists.txt
├── cmake/
│   ├── FindFPC.cmake
│   └── FindLazarus.cmake
└── Projects/
    ├── realms.lpi  # Lazarus project
    └── realms.lpr  # Free Pascal client (realms-pascal)
```

### Compilation Errors

**Problem**: FPC compilation fails

**Solution**:
```bash
# Check FPC version (needs 3.2.0+)
fpc -iV

# Try verbose build to see actual error
cmake --build . --verbose

# Try building with FPC directly to see the actual error (realms-pascal;
# needs libekron_sandbox from the CMake build in ../build)
cd Projects
fpc -Mobjfpc -Sh -Fu../realms/pascal -Fi../realms/pascal -Fl../build realms.lpr
```

### Wrong Architecture on macOS

**Problem**: Building for wrong architecture (Intel vs ARM)

**Solution**:
```bash
# Check what FPC is targeting
fpc -iTP  # Shows target processor
fpc -iTO  # Shows target OS

# If wrong, you may need to reinstall FPC for your architecture
arch      # Shows your current architecture
uname -m  # Shows machine hardware name
```

### Windows: "lazbuild not found"

**Problem**: Lazarus not in PATH

**Solution**:
```cmd
REM Add Lazarus to PATH temporarily
set PATH=%PATH%;C:\lazarus

REM Or install it in PATH permanently via System Settings
REM Or specify in CMake:
cmake -DLAZBUILD_EXECUTABLE=C:/lazarus/lazbuild.exe ..
```

### Linux: Permission Denied

**Problem**: Can't execute built binary

**Solution**:
```bash
# Make it executable
chmod +x realms

# Or run with explicit path
./realms
```

---

## Project Structure

Understanding the project layout:

```
Realms/
├── CMakeLists.txt              # Main CMake configuration
├── BUILD.md                    # This file
├── README.md                   # Project readme
├── LICENSE                     # License file
├── cmake/
│   ├── FindFPC.cmake          # FPC detection module
│   └── FindLazarus.cmake      # Lazarus detection module
├── Projects/                   # Main project files (note: capital P)
│   ├── realms.lpi             # Lazarus project
│   ├── realms.lpr             # Free Pascal client (realms-pascal)
│   └── *.pas                  # Additional units
├── src/                       # Optional additional sources
├── units/                     # Optional unit directories
└── build/                     # Build directory (created by you)
```

---

## Getting Help

If you encounter issues:

1. Check this BUILD.md file
2. Check the CMake output for warnings/errors
3. Try building with verbose output: `cmake --build . --verbose`
4. Check FPC/Lazarus versions: `fpc -iV` and check Lazarus About dialog
5. Open an issue on the project repository with:
   - Your OS and version
   - CMake version (`cmake --version`)
   - FPC version (`fpc -iV`)
   - Lazarus version (if using)
   - Full error message
   - CMake configuration output

---

## Additional Resources

- [CMake Documentation](https://cmake.org/documentation/)
- [Free Pascal Documentation](https://www.freepascal.org/docs.html)
- [Lazarus Documentation](https://wiki.lazarus.freepascal.org/)
- [FPC Compiler Options](https://www.freepascal.org/docs-html/user/user.html)