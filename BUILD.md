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

1. Download and install [CMake](https://cmake.org/download/)
2. Download and install [Lazarus](https://www.lazarus-ide.org/index.php?page=downloads) (includes FPC)
   - Recommended: Use the 64-bit installer
   - Default location: `C:\lazarus`

Alternatively, install FPC standalone from [FreePascal.org](https://www.freepascal.org/download.html)

#### Linux (Debian/Ubuntu)

```bash
sudo apt update
sudo apt install cmake build-essential
sudo apt install fpc
sudo apt install lazarus

# Or from source
# https://www.freepascal.org/download.html
```

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
./realms          # Linux/macOS
.\realms.exe      # Windows
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

#### Using Command Prompt

```cmd
REM Create build directory
mkdir build
cd build

REM Configure (64-bit)
cmake -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release ..

REM Or using Visual Studio generator
cmake -G "Visual Studio 17 2022" -DCMAKE_BUILD_TYPE=Release ..

REM Build
cmake --build . --config Release

REM Run
realms.exe
```

#### Using PowerShell

```powershell
# Create build directory
New-Item -ItemType Directory -Force -Path build
Set-Location build

# Configure
cmake -DCMAKE_BUILD_TYPE=Release ..

# Build
cmake --build . --config Release

# Run
.\realms.exe
```

#### Using Developer Command Prompt

If you have Visual Studio installed:

```cmd
mkdir build && cd build
cmake -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release ..
nmake
realms.exe
```

#### Creating Windows Installer

```cmd
cmake --build . --config Release
cpack -G NSIS

REM This creates an installer .exe in the build directory
```

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

To build it by hand once `libekron_sandbox` exists, from `Projects/`: `fpc -Mobjfpc -Sh -O2 -Fu../realms/pascal -Fi../realms/pascal -Fl../build realms.lpr`. The old Quake 2 client graph that used to be `realms.lpr` is kept as `Projects/realms_q2legacy.lpr`. It does not build: six `sys/linux` units it names don't exist, and the engine units don't compile together yet.

### Pascal SDL2 Units and Other Submodules

The legacy Free Pascal sys layer (`sys/mac/sys_mac.pas`, `Projects/realms.lpi`) uses the SDL2 units in `tools/SDL2-for-Pascal`. That folder, `tools/SDL3-for-Pascal` and `ide` (KayteIDE) are git submodules. The desktop C++ client, `realms-pascal`, the launcher and the Realms core don't need them. Fetch the SDL2 units with:

```bash
git submodule update --init tools/SDL2-for-Pascal
# or everything: git submodule update --init
```

Units listed with `in '...'` resolve from the directory fpc runs in. `Projects/realms_q2legacy.lpr` and `realms.lpi` compile from `Projects/`. `tests/pascal/sdl2_sys_smoke.lpr` uses SDL2 the same way `sys_mac.pas` does, and the desktop CI builds and runs it:

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

For Android `jniLibs` and the iOS static archive, use `realms/pascal/build_core.sh android-aarch64|android-armv7|android-x86_64|ios-arm64`. Without a Pascal core, every platform falls back to the C++ reference core.

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
cpack -G ZIP        # ZIP archive (all platforms)
cpack -G NSIS       # Windows installer
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