// GLES 3 drawing of Quake2View's entities (.md3 with TGA / PCX skins, fallback
// boxes, view weapon) and effect quads. Uses the projection of the sandbox
// program in Renderer.cpp.
#pragma once

#include "Quake2View.h"

#include "realms/Engine.h"
#include "realms/ViewFov.h"

#include <GLES3/gl3.h>

#include <string>
#include <unordered_map>
#include <vector>

class Quake2Gles {
public:
    struct Camera {
        physics::Vec3 eye{};
        float yaw = 0.0f, pitch = 0.0f, aspect = 1.0f; // the sandbox camera convention
        float focal = realms::kSandboxFocalLength;    // realms::mapFocalLength(fov) in map mode
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
