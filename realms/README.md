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
 │ realms_bridge.h: RealmsHostApi  ⇄  RealmsCoreApi (v1)   │
 └───────┬──────────────────────────────────┬──────────────┘
         │ dlopen / static link             │ built in
 ┌───────┴───────────────┐        ┌─────────┴────────────┐
 │ librealms_core         │        │ NativeCore.cpp        │
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

BSP maps, the renderer, sound, the server and the game DLL are **not** part of this step.

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

## Shared boot (every platform)

`realms::sandbox::boot()` runs the same steps on every platform:

1. Registers `sensitivity`, `m_invert`, `name`, and `realms_platform`. `realms_platform` is read-only and holds `desktop-linux`, `desktop-macos`, `android` or `ios`.
2. Mounts `pak0.pak` … `pak9.pak`.
3. Executes `config/realms.cfg`, then `config.cfg` (archived cvars, saved with `writeconfig`), then `autoexec.cfg`.
4. Runs `+command` arguments from the command line (desktop only).

Mouse and touch look use `sensitivity` and `m_invert` on all three platforms.

## Building the Pascal core

| Platform | How |
| :--- | :--- |
| Linux / macOS | The desktop CMake build adds the `realms_pascal_core` target when `fpc` is installed. It is controlled by `-DENABLE_PASCAL_CORE=ON`, which is the default. |
| Android | `realms/pascal/build_core.sh android-aarch64` writes to `sys/android/app/src/main/jniLibs/arm64-v8a/`. It needs an FPC cross compiler and `ANDROID_NDK_HOME`. |
| iOS | `realms/pascal/build_core.sh ios-arm64` produces `librealms_core.a`. Link it in Xcode and add `REALMS_PASCAL_STATIC=1` to the preprocessor definitions. |
| Any CMake consumer | `-DREALMS_PASCAL_STATIC_LIBRARY=/path/librealms_core.a` links a static Pascal core into `realms_engine`. |

## Tests

`tests/realms/realms_bridge_test.cpp` is the ABI conformance suite. It covers cvar semantics, the command buffer, aliases and macros, PAK mounting, `writeconfig`, error paths, and the Engine facade. It runs against:

- the C++ core (CTest `realms_bridge_native`; Android/iOS CI);
- the Pascal core (CTest `realms_bridge_pascal`, i.e. `realms_bridge_test --pascal <lib>`, when `fpc` is available).

On Linux and macOS, CI also starts the packaged sandbox with `REALMS_CORE=pascal --headless-smoke`. The run must report `realmsCore=pascal-quake2`.

```bash
g++ -std=c++17 -Wall -Wextra -Werror -pedantic -Irealms/include \
  tests/realms/realms_bridge_test.cpp realms/src/Engine.cpp \
  realms/src/NativeCore.cpp realms/src/SandboxBoot.cpp -ldl -o /tmp/realms-test
/tmp/realms-test                                  # C++ reference core
/tmp/realms-test --pascal build/librealms_core.so # Free Pascal core
```

## ABI rules

- Memory is freed by the side that allocated it:
  - host blobs with `host->free_file`;
  - core blobs with `core->fs_free`;
  - strings are copied into buffers the caller provides.
- Every Pascal export is wrapped in `try/except`, so no exception crosses into C++. Floating-point exceptions are masked on entry.
- `rlm_bridge.pas` mirrors `realms_bridge.h` field for field (`{$PACKRECORDS C}`, `cdecl`). If you change one, change the other and bump `REALMS_BRIDGE_VERSION`.
