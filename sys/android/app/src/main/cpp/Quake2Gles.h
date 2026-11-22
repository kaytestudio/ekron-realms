// Realms map mode on Android: the Quake 2 module's entities and effects on
// GLES 3 (mapview::Quake2View hands them out in render space).
//
//   * entities: .md2 models with their PCX skins (uploaded on first use,
//     kept until release()), fallback boxes and the view weapon (drawn in
//     front of the world, like Quake 2's depth hack), lit by the light under
//     them from the map's lightmaps (or the sandbox sun) and the dynamic
//     lights; translucent ones (alpha < 1, shells) blended last;
//   * effects: particles, glows and beams as camera-facing quads, blended.
//
// The projection is the sandbox program's (Renderer.cpp), so everything
// lines up with the map drawn by that program.
#pragma once

#include "Quake2View.h"

#include "realms/Engine.h"

#include <GLES3/gl3.h>

#include <string>
#include <unordered_map>
#include <vector>

class Quake2Gles {
public:
    struct Camera {
        physics::Vec3 eye{};
        float yaw = 0.0f, pitch = 0.0f, aspect = 1.0f; // the sandbox camera convention
    };

    Quake2Gles() = default;
    Quake2Gles(const Quake2Gles &) = delete;
    Quake2Gles &operator=(const Quake2Gles &) = delete;

    // Compiles the programs on first use (GL context current); false when
    // they don't build (entities and effects are then skipped).
    bool ensure();
    // Every GL object and skin (GL context current).
    void release();

    void drawEntities(realms::Engine &engine, const std::vector<mapview::EntityVertex> &vertices,
                      const std::vector<mapview::EntityBatch> &batches, const std::vector<mapview::DynamicLight> &lights,
                      const Camera &camera);
    void drawEffects(const std::vector<mapview::EffectVertex> &vertices, const Camera &camera);

    size_t skinsLoaded() const;

private:
    GLuint skin(realms::Engine &engine, const std::string &name);

    bool tried_ = false;
    GLuint entityProgram_ = 0, effectProgram_ = 0;
    GLuint entityVao_ = 0, entityVbo_ = 0, effectVao_ = 0, effectVbo_ = 0;
    std::unordered_map<std::string, GLuint> skins_; // 0: tried, not loadable
};
