// Realms map mode on Android: "map <name>" (pause menu > Maps, or any
// script) swaps the sandbox for a Quake 2 BSP or glTF map loaded into the
// Realms core, walked with realms::MapWalker and drawn with the sandbox's
// GLES program. Spaces and the walker are mapview::MapSession, shared with
// the desktop client.
//
// Drawing:
//   * the sandbox's own city, already on the GPU, when the map is that same
//     file - no second copy of a 14 MB model; it is a level-mode scene, so
//     the camera moves into its space (Scene::sourceScale / sourceOffset);
//   * other glTF maps: the textured file, prepared on a worker thread and
//     uploaded a few steps per frame; until then, and for BSPs, the map's
//     own triangles with the sandbox material layers.
#pragma once

#include "GltfRenderer.h"
#include "MapSession.h"

#include <GLES3/gl3.h>

#include <array>
#include <future>
#include <memory>
#include <optional>
#include <string>

class AndroidMapMode {
public:
    AndroidMapMode() = default;
    AndroidMapMode(const AndroidMapMode &) = delete;
    AndroidMapMode &operator=(const AndroidMapMode &) = delete;
    ~AndroidMapMode();

    // Opens the engine's loaded map (GL context current). `sandboxCity` and
    // its asset path let the sandbox city model be reused.
    bool open(realms::Engine &engine, const city::GltfRenderer &sandboxCity, const std::string &sandboxCityPath,
              std::string &error);
    void close(); // GL context current
    bool active() const { return session_.active(); }
    // The textured model is still being prepared / uploaded.
    bool texturedPending() const { return prepare_.valid() || model_.uploading(); }

    // Continues preparing / uploading the textured model (GL context current).
    void advance();
    void update(const realms::Engine &engine, const realms::WalkInput &input, float renderYaw, float renderPitch,
                float dt) {
        session_.update(engine, input, renderYaw, renderPitch, dt);
    }
    bool respawn(const realms::Engine &engine) { return session_.respawn(engine); }

    // Camera eye in the space draw() renders in (call before setting the
    // shader's eye uniform) and a draw-order hint for blended meshes.
    physics::Vec3 drawEye() const;
    // Draws the map with `program` bound and the per-frame uniforms set.
    // The vertex-colour pass expects the sandbox albedo array on unit 0.
    void draw(GLuint program, const city::GltfRenderer &sandboxCity) const;

    mapview::MapSession &session() { return session_; }
    const mapview::MapSession &session() const { return session_; }

private:
    void releaseTriangles();

    mapview::MapSession session_;
    bool reuseCity_ = false;
    float cityScale_ = 1.0f;
    std::array<float, 3> cityOffset_{};
    GLuint vao_ = 0, vbo_ = 0;
    GLsizei count_ = 0;
    city::GltfRenderer model_;
    std::future<std::optional<city::Scene>> prepare_;
};
