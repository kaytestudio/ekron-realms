// "map <name>" on Android: a Quake 2 BSP or glTF map drawn with the
// sandbox's GLES program, on top of mapview::Quake2View. Runs the game when
// librealms_game.so is in the APK and the map has a worldspawn; otherwise
// the map is walked.
//
// When the map is the sandbox's own city file the already uploaded model is
// reused (level-mode scene, see Scene::sourceScale/sourceOffset). Other glTF
// maps are prepared on a worker thread; until then, and for BSPs, the map's
// own triangles are drawn, grouped by BSP model and lightmapped.
#pragma once

#include "GameSoundPlayer.h"
#include "GltfRenderer.h"
#include "Quake2Gles.h"
#include "Quake2View.h"

#include <GLES3/gl3.h>

#include <array>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class AndroidMapMode {
public:
    AndroidMapMode() = default;
    AndroidMapMode(const AndroidMapMode &) = delete;
    AndroidMapMode &operator=(const AndroidMapMode &) = delete;
    ~AndroidMapMode();

    // Opens the engine's loaded map (GL context current). `sandboxCity` and
    // its asset path let the sandbox city model be reused. `retained` is a
    // view kept from the previous Renderer (releaseView): when it is still
    // playing this map its game carries on instead of restarting.
    bool open(realms::Engine &engine, const city::GltfRenderer &sandboxCity, const std::string &sandboxCityPath,
              std::string &error, std::unique_ptr<mapview::Quake2View> retained = nullptr);
    void close(); // GL context current
    // The map and its running game, kept while the GL context goes away (the
    // GL objects are released here). Inactive afterwards.
    std::unique_ptr<mapview::Quake2View> releaseView();
    bool active() const { return view_ && view_->active(); }
    // The textured model is still being prepared / uploaded.
    bool texturedPending() const { return prepare_.valid() || model_.uploading(); }

    // Continues preparing / uploading the textured model (GL context current).
    void advance();
    // One frame of the game (attack fires) or the walker.
    void update(realms::Engine &engine, const realms::WalkInput &input, float renderYaw, float renderPitch, float dt,
                bool attack);
    bool respawn(realms::Engine &engine);
    void toggleNoclip();
    float spawnRenderYaw() const;

    // Camera eye in the space draw() renders in (call before setting the
    // shader's eye uniform).
    physics::Vec3 drawEye() const;
    // Draws the map with `program` bound and the per-frame uniforms set,
    // then the game's entities and effects. The vertex-colour pass expects
    // the sandbox albedo array on unit 0.
    void draw(GLuint program, const city::GltfRenderer &sandboxCity, realms::Engine &engine, float renderYaw,
              float renderPitch, float aspect);
    // The game's sounds on the sandbox mixer (silent while paused).
    void playSounds(realms::Engine &engine, ekron::audio::Mixer &mixer, double dt, bool paused);

    bool gameRunning() const { return view_ && view_->gameRunning(); }
    realms::Quake2Module::PlayerStats stats() const;
    std::vector<std::string> takeMessages();

    mapview::MapSession &session() { return view_->session(); }
    const mapview::MapSession &session() const { return view_->session(); }

private:
    void releaseTriangles();
    void releaseGl();

    std::unique_ptr<mapview::Quake2View> view_ = std::make_unique<mapview::Quake2View>();
    bool reuseCity_ = false;
    float cityScale_ = 1.0f;
    std::array<float, 3> cityOffset_{};
    GLuint vao_ = 0, vbo_ = 0, lightUvVbo_ = 0, lightmapTexture_ = 0;
    GLsizei count_ = 0;
    std::vector<mapview::ModelRange> ranges_;
    city::GltfRenderer model_;
    std::future<std::optional<city::Scene>> prepare_;
    Quake2Gles gles_;
    ekron::audio::GameSoundPlayer sound_;
    ekron::audio::Mixer *mixer_ = nullptr;
    std::vector<mapview::EntityVertex> entityVertices_;
    std::vector<mapview::EntityBatch> entityBatches_;
    std::vector<mapview::EffectVertex> effectVertices_;
    std::vector<mapview::DynamicLight> lights_;
};
