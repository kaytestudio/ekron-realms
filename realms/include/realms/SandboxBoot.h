// Boot sequence shared by desktop, Android and iOS:
//
//   1. pak0.pak ... pak9.pak from the asset roots
//   2. exec config/realms.cfg   (shipped defaults)
//   3. exec config.cfg          (saved settings)
//   4. exec autoexec.cfg        (user overrides)
//   5. "+set x y +cmd" from the command line
//
// boot() also registers the map commands, so "+map base1" works everywhere.
#pragma once

#include "realms/Engine.h"

#include <functional>
#include <string>

namespace realms::sandbox {

// Called after every map change with the loaded map path ("" = no map).
using MapChangedFn = std::function<void(const std::string &path)>;

struct BootOptions {
    // "desktop-linux", "desktop-macos", "desktop-windows", "handheld-linux", "android",
    // "ios" (NOSET cvar realms_platform).
    std::string platform;
    // Quake 2 command-line script (see Engine::commandLineScript).
    std::string commandLine;
    // Optional existence probe used before mounting PAKs (avoids reading
    // multi-megabyte archives just to check for them).
    std::function<bool(const std::string &path)> fileExists;
    bool execUserConfig = true;
    // Map console commands (see registerMapCommands).
    bool mapCommands = true;
    MapChangedFn onMapChanged;
};

// Cvars shared by every platform (sensitivity, m_invert, name, s_volume,
// s_musicvolume...).
void registerSharedCvars(Engine &engine, const std::string &platform);

// Quake 2 style map commands shared by every host:
//   map <name>   loads maps/<name>.bsp / .glb / .gltf (or a path); sets the
//                NOSET cvar "mapname"
//   mapinfo      lump counts, bounds and entities of the loaded map
//   killserver   unloads the map
//   load <name>  loads a save game (GameSave.h): its map, then the game reads
//                it back when it starts there
// Also registers map_mesh_scale (units per glTF metre, default 32) and
// map_mesh_thickness (collision slab thickness, 0 = 2 units).
void registerMapCommands(Engine &engine, MapChangedFn onChange = {});

// Runs the full boot sequence. Returns the number of PAKs mounted.
int boot(Engine &engine, const BootOptions &options);

// Persists archived cvars to config.cfg in the writable root.
void saveUserConfig(Engine &engine);

// Mouse/touch look multiplier: 1.0 at the Quake 2 default sensitivity 3.
float lookScale(const Engine &engine);
// -1 when m_invert is set, +1 otherwise.
float pitchSign(const Engine &engine);

} // namespace realms::sandbox
