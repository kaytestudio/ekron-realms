#pragma once
// Every game sound by name: the files in assets/sounds/ (see LICENSE.md there)
// and how each one plays. `variants` > 0 means name_1.ogg .. name_N.ogg, one
// picked at random per play; 0 means name.ogg.

#include <cstddef>
#include <string>

namespace ekron::audio {

struct SoundDef {
    const char *name;
    int variants;
    float volume;   // base volume, before s_volume
    bool stream;    // decoded while playing (long files) instead of up front
    int bus;        // 0 effects, 1 music, 2 interface
};

inline constexpr SoundDef kSounds[] = {
    {"step_concrete", 5, 0.3f, false, 0},
    {"step_wood", 5, 0.3f, false, 0},
    {"step_grass", 5, 0.38f, false, 0},
    {"step_carpet", 5, 0.6f, false, 0},
    {"step_snow", 5, 0.7f, false, 0},
    {"jump", 0, 0.6f, false, 0},
    {"land", 2, 0.6f, false, 0},
    {"grab", 0, 0.35f, false, 0},
    {"throw", 0, 0.4f, false, 0},
    {"freeze", 0, 0.35f, false, 0},
    {"spawn", 0, 0.7f, false, 0},
    {"delete", 0, 0.35f, false, 0},
    {"tool_switch", 0, 0.35f, false, 2},
    {"door_open", 0, 0.5f, false, 0},
    {"door_close", 0, 0.5f, false, 0},
    {"car_door_open", 0, 0.45f, false, 0},
    {"car_door_close", 0, 0.45f, false, 0},
    {"impact_wood", 3, 0.5f, false, 0},
    {"impact_metal", 3, 0.55f, false, 0},
    {"impact_generic", 3, 0.45f, false, 0},
    {"engine", 0, 0.5f, false, 0},
    {"drone", 0, 0.5f, false, 0},
    {"ambience_city", 0, 0.5f, true, 0},
    {"music_infinite_peace", 0, 0.8f, true, 1},
    {"ui_click", 0, 0.5f, false, 2},
    {"ui_open", 0, 0.45f, false, 2},
    {"ui_close", 0, 0.45f, false, 2},
    {"ui_confirm", 0, 0.5f, false, 2},
    {"ui_error", 0, 0.45f, false, 2},
};

inline constexpr size_t kSoundCount = sizeof(kSounds) / sizeof(kSounds[0]);

inline const SoundDef *findSound(const std::string &name) {
    for (const auto &s : kSounds)
        if (name == s.name) return &s;
    return nullptr;
}

// Asset path of one file ("sounds/step_wood_3.ogg"); variant is 1-based and
// ignored for sounds without variants.
inline std::string soundFile(const SoundDef &s, int variant) {
    std::string path = std::string("sounds/") + s.name;
    if (s.variants > 0) path += "_" + std::to_string(variant);
    return path + ".ogg";
}

// Clip name in the mixer: the file name without folder and extension.
inline std::string soundClipName(const SoundDef &s, int variant) {
    return s.variants > 0 ? std::string(s.name) + "_" + std::to_string(variant) : std::string(s.name);
}

} // namespace ekron::audio
